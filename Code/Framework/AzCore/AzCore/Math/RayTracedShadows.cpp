/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Math/RayTracedShadows.h>

#include <AzCore/Math/MathUtils.h>
#include <AzCore/std/algorithm.h>

#include <cmath>

namespace AZ
{
    namespace
    {
        // Branchless orthonormal basis around the unit vector n (Duff et al. 2017).
        void BuildOrthonormalBasis(const Vector3& n, Vector3& tangent, Vector3& bitangent)
        {
            const float sign = n.GetZ() >= 0.0f ? 1.0f : -1.0f;
            const float a = -1.0f / (sign + n.GetZ());
            const float b = n.GetX() * n.GetY() * a;
            tangent = Vector3(1.0f + sign * n.GetX() * n.GetX() * a, sign * b, -sign * n.GetX());
            bitangent = Vector3(b, sign + n.GetY() * n.GetY() * a, -n.GetY());
        }

        // Vogel (golden-angle) disk point in the unit disk; evenly covers it for any sample count.
        void VogelDiskPoint(uint32_t sampleIndex, uint32_t sampleCount, float rotation, float& x, float& y)
        {
            // Golden angle, pi * (3 - sqrt(5)).
            constexpr float GoldenAngle = 2.39996323f;
            const float radius = Sqrt((static_cast<float>(sampleIndex) + 0.5f) / static_cast<float>(AZStd::max(1u, sampleCount)));
            const float theta = static_cast<float>(sampleIndex) * GoldenAngle + rotation;
            x = radius * Cos(theta);
            y = radius * Sin(theta);
        }

        // Adaptive multi-ray visibility shared by the directional and local-light paths. Probes
        // first: the last probeCount Vogel samples lie on the outer ring of the disk and, being
        // consecutive golden-angle steps, are spread around it, so any straight shadow edge crossing
        // the disk splits them unless it only clips a thin sliver.
        template<typename OccludedFn>
        float ComputeMultiRayVisibility(uint32_t sampleCount, bool adaptive, const OccludedFn& occluded)
        {
            const uint32_t probeCount = GetSoftShadowProbeCount(sampleCount);
            const uint32_t firstProbe = sampleCount - probeCount;
            uint32_t hits = 0;
            for (uint32_t index = firstProbe; index < sampleCount; ++index)
            {
                hits += occluded(index) ? 1 : 0;
            }
            if (adaptive && (hits == 0 || hits == probeCount))
            {
                return hits == 0 ? 1.0f : 0.0f;
            }

            for (uint32_t index = 0; index < firstProbe; ++index)
            {
                hits += occluded(index) ? 1 : 0;
            }
            return 1.0f - static_cast<float>(hits) / static_cast<float>(sampleCount);
        }
    } // namespace
    float ComputeShadowVisibility(
        const RayTracingBvh& bvh, const ShadowSample& sample, const ShadowRayParams& params)
    {
        // Offset the origin off the surface along the normal to avoid the surface shadowing itself
        // (shadow acne) at the ray origin.
        const Vector3 origin = sample.m_position + sample.m_normal * params.m_normalBias;
        const Vector3 direction = params.m_toLight.GetNormalizedSafe();

        // Occluded => in shadow (0), otherwise fully lit (1). IntersectAny early-outs on first hit.
        return bvh.IntersectAny(origin, direction, params.m_maxDistance) ? 0.0f : 1.0f;
    }

    void ComputeDirectionalShadowVisibility(
        const RayTracingBvh& bvh,
        const AZStd::vector<ShadowSample>& samples,
        const ShadowRayParams& params,
        AZStd::vector<float>& outVisibility)
    {
        outVisibility.resize(samples.size());
        for (size_t i = 0; i < samples.size(); ++i)
        {
            outVisibility[i] = ComputeShadowVisibility(bvh, samples[i], params);
        }
    }

    uint32_t GetSoftShadowProbeCount(uint32_t sampleCount)
    {
        return AZStd::min(sampleCount, AZStd::max(2u, sampleCount / 4u));
    }

    Vector3 ComputeSoftShadowRayDirection(
        const Vector3& toLight, float tanAngularRadius, uint32_t sampleIndex, uint32_t sampleCount, float rotation)
    {
        const Vector3 n = toLight.GetNormalizedSafe();
        Vector3 tangent, bitangent;
        BuildOrthonormalBasis(n, tangent, bitangent);
        float diskX, diskY;
        VogelDiskPoint(sampleIndex, sampleCount, rotation, diskX, diskY);
        return (n + tangent * (diskX * tanAngularRadius) + bitangent * (diskY * tanAngularRadius)).GetNormalizedSafe();
    }

    float ComputeSoftShadowVisibility(
        const RayTracingBvh& bvh, const ShadowSample& sample, const ShadowRayParams& params, const SoftShadowParams& soft)
    {
        if (soft.m_sampleCount <= 1 || soft.m_angularRadius <= 0.0f)
        {
            return ComputeShadowVisibility(bvh, sample, params);
        }

        const uint32_t sampleCount = soft.m_sampleCount;
        const float tanRadius = std::tan(soft.m_angularRadius);
        const Vector3 origin = sample.m_position + sample.m_normal * params.m_normalBias;
        return ComputeMultiRayVisibility(
            sampleCount,
            soft.m_adaptive,
            [&](uint32_t index)
            {
                const Vector3 direction = ComputeSoftShadowRayDirection(params.m_toLight, tanRadius, index, sampleCount, soft.m_rotation);
                return bvh.IntersectAny(origin, direction, params.m_maxDistance);
            });
    }

    float ComputeLocalLightIlluminance(const LocalShadowLight& light, const Vector3& receiver)
    {
        const Vector3 toLight = light.m_position - receiver;
        const float distanceSquared = toLight.GetLengthSq();
        const float falloff = distanceSquared * light.m_invAttenuationRadiusSquared;
        if (falloff >= 1.0f || light.m_intensity <= 0.0f)
        {
            return 0.0f;
        }
        float radiusAttenuation = 1.0f - falloff * falloff;
        radiusAttenuation *= radiusAttenuation;
        float illuminance = light.m_intensity / AZStd::max(distanceSquared, 1.0e-6f) * radiusAttenuation;

        if (!light.m_spotDirection.IsZero())
        {
            const float cosAngle = (-toLight.GetNormalizedSafe()).Dot(light.m_spotDirection.GetNormalizedSafe());
            if (cosAngle <= light.m_cosOuterConeAngle)
            {
                return 0.0f;
            }
            if (cosAngle < light.m_cosInnerConeAngle)
            {
                const float t = (cosAngle - light.m_cosOuterConeAngle) / (light.m_cosInnerConeAngle - light.m_cosOuterConeAngle);
                illuminance *= t * t * (3.0f - 2.0f * t);
            }
        }
        return illuminance;
    }

    Vector3 ComputeLocalLightSamplePoint(
        const LocalShadowLight& light, const Vector3& receiver, uint32_t sampleIndex, uint32_t sampleCount, float rotation)
    {
        if (sampleCount <= 1 || light.m_radius <= 0.0f)
        {
            return light.m_position;
        }
        const Vector3 n = light.m_diskNormal.IsZero() ? (light.m_position - receiver).GetNormalizedSafe()
                                                      : light.m_diskNormal.GetNormalizedSafe();
        Vector3 tangent, bitangent;
        BuildOrthonormalBasis(n, tangent, bitangent);
        float diskX, diskY;
        VogelDiskPoint(sampleIndex, sampleCount, rotation, diskX, diskY);
        return light.m_position + (tangent * diskX + bitangent * diskY) * light.m_radius;
    }

    float ComputeLocalLightShadowVisibility(
        const RayTracingBvh& bvh,
        const ShadowSample& sample,
        const LocalShadowLight& light,
        float normalBias,
        float clearance,
        const SoftShadowParams& soft)
    {
        const Vector3 origin = sample.m_position + sample.m_normal * normalBias;
        const uint32_t sampleCount = light.m_radius > 0.0f ? AZStd::max(1u, soft.m_sampleCount) : 1u;
        auto occluded = [&](uint32_t index)
        {
            const Vector3 toTarget = ComputeLocalLightSamplePoint(light, sample.m_position, index, sampleCount, soft.m_rotation) - origin;
            const float distance = toTarget.GetLength();
            const float tMax = distance - clearance;
            return tMax > 0.0f && bvh.IntersectAny(origin, toTarget / distance, tMax);
        };
        if (sampleCount == 1)
        {
            return occluded(0) ? 0.0f : 1.0f;
        }
        return ComputeMultiRayVisibility(sampleCount, soft.m_adaptive, occluded);
    }

    float CombineShadowVisibility(const AZStd::vector<WeightedShadowVisibility>& lights)
    {
        float total = 0.0f;
        float visible = 0.0f;
        for (const WeightedShadowVisibility& light : lights)
        {
            total += light.m_weight;
            visible += light.m_weight * light.m_visibility;
        }
        return total > 0.0f ? visible / total : 1.0f;
    }

    void SelectShadowLights(AZStd::vector<LocalShadowLight>& lights, const Vector3& camera, float maxDistance, uint32_t maxCount)
    {
        auto score = [&camera](const LocalShadowLight& light)
        {
            return light.m_intensity / AZStd::max(1.0f, light.m_position.GetDistanceSq(camera));
        };
        lights.erase(
            AZStd::remove_if(
                lights.begin(),
                lights.end(),
                [&](const LocalShadowLight& light)
                {
                    const float range = light.m_invAttenuationRadiusSquared > 0.0f ? 1.0f / Sqrt(light.m_invAttenuationRadiusSquared) : 1.0e30f;
                    return light.m_intensity <= 0.0f || light.m_position.GetDistance(camera) - range > maxDistance;
                }),
            lights.end());
        AZStd::sort(
            lights.begin(),
            lights.end(),
            [&](const LocalShadowLight& a, const LocalShadowLight& b)
            {
                return score(a) > score(b);
            });
        if (lights.size() > maxCount)
        {
            lights.resize(maxCount);
        }
    }
} // namespace AZ
