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
        // Golden angle, pi * (3 - sqrt(5)).
        constexpr float GoldenAngle = 2.39996323f;

        const Vector3 n = toLight.GetNormalizedSafe();

        // Branchless orthonormal basis around n (Duff et al. 2017).
        const float sign = n.GetZ() >= 0.0f ? 1.0f : -1.0f;
        const float a = -1.0f / (sign + n.GetZ());
        const float b = n.GetX() * n.GetY() * a;
        const Vector3 tangent(1.0f + sign * n.GetX() * n.GetX() * a, sign * b, -sign * n.GetX());
        const Vector3 bitangent(b, sign + n.GetY() * n.GetY() * a, -n.GetY());

        // Vogel disk: evenly covers the unit disk for any sample count.
        const float radius = Sqrt((static_cast<float>(sampleIndex) + 0.5f) / static_cast<float>(AZStd::max(1u, sampleCount)));
        const float theta = static_cast<float>(sampleIndex) * GoldenAngle + rotation;
        const float diskX = radius * Cos(theta) * tanAngularRadius;
        const float diskY = radius * Sin(theta) * tanAngularRadius;

        return (n + tangent * diskX + bitangent * diskY).GetNormalizedSafe();
    }

    float ComputeSoftShadowVisibility(
        const RayTracingBvh& bvh, const ShadowSample& sample, const ShadowRayParams& params, const SoftShadowParams& soft)
    {
        if (soft.m_sampleCount <= 1 || soft.m_angularRadius <= 0.0f)
        {
            return ComputeShadowVisibility(bvh, sample, params);
        }

        const uint32_t sampleCount = soft.m_sampleCount;
        const uint32_t probeCount = GetSoftShadowProbeCount(sampleCount);
        const float tanRadius = std::tan(soft.m_angularRadius);
        const Vector3 origin = sample.m_position + sample.m_normal * params.m_normalBias;

        auto occluded = [&](uint32_t index)
        {
            const Vector3 direction = ComputeSoftShadowRayDirection(params.m_toLight, tanRadius, index, sampleCount, soft.m_rotation);
            return bvh.IntersectAny(origin, direction, params.m_maxDistance);
        };

        // Probes first: the last probeCount Vogel samples lie on the outer ring of the disk and,
        // being consecutive golden-angle steps, are spread around it, so any straight shadow edge
        // crossing the disk splits them unless it only clips a thin sliver.
        const uint32_t firstProbe = sampleCount - probeCount;
        uint32_t hits = 0;
        for (uint32_t index = firstProbe; index < sampleCount; ++index)
        {
            hits += occluded(index) ? 1 : 0;
        }
        if (soft.m_adaptive && (hits == 0 || hits == probeCount))
        {
            return hits == 0 ? 1.0f : 0.0f;
        }

        for (uint32_t index = 0; index < firstProbe; ++index)
        {
            hits += occluded(index) ? 1 : 0;
        }
        return 1.0f - static_cast<float>(hits) / static_cast<float>(sampleCount);
    }
} // namespace AZ
