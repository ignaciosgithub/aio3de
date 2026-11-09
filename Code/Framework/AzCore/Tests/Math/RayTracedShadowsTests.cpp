/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Math/RayTracedShadows.h>
#include <AzCore/Math/RayTracingBvh.h>
#include <AzCore/UnitTest/TestTypes.h>
#include <AzCore/Math/MathUtils.h>
#include <AzCore/std/algorithm.h>
#include <cmath>
#include <random>

namespace UnitTest
{
    using AZ::BvhTriangle;
    using AZ::RayTracingBvh;
    using AZ::ShadowRayParams;
    using AZ::ShadowSample;
    using AZ::SoftShadowParams;
    using AZ::Vector3;

    // Builds two triangles spanning [minXY, maxXY] in the z = z plane (an axis-aligned quad occluder).
    static void AppendQuad(
        AZStd::vector<BvhTriangle>& tris, float minX, float minY, float maxX, float maxY, float z)
    {
        const Vector3 a(minX, minY, z);
        const Vector3 b(maxX, minY, z);
        const Vector3 c(maxX, maxY, z);
        const Vector3 d(minX, maxY, z);
        tris.push_back({ a, b, c });
        tris.push_back({ a, c, d });
    }

    TEST(MATH_RayTracedShadows, EmptyOccluderSceneIsFullyLit)
    {
        RayTracingBvh bvh;
        bvh.Build({});

        ShadowSample sample{ Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f) };
        ShadowRayParams params;
        params.m_toLight = Vector3(0.0f, 0.0f, 1.0f);
        params.m_maxDistance = 100.0f;

        EXPECT_NEAR(AZ::ComputeShadowVisibility(bvh, sample, params), 1.0f, 1e-6f);
    }

    TEST(MATH_RayTracedShadows, OccluderBetweenSurfaceAndLightCastsShadow)
    {
        // A small quad occluder floating at z = 5 directly above the origin.
        AZStd::vector<BvhTriangle> tris;
        AppendQuad(tris, -1.0f, -1.0f, 1.0f, 1.0f, 5.0f);
        RayTracingBvh bvh;
        bvh.Build(tris);

        ShadowRayParams params;
        params.m_toLight = Vector3(0.0f, 0.0f, 1.0f); // light straight up
        params.m_maxDistance = 100.0f;

        // Surface point under the occluder -> shadowed.
        const ShadowSample under{ Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f) };
        EXPECT_NEAR(AZ::ComputeShadowVisibility(bvh, under, params), 0.0f, 1e-6f);

        // Surface point well to the side of the occluder -> lit.
        const ShadowSample beside{ Vector3(10.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f) };
        EXPECT_NEAR(AZ::ComputeShadowVisibility(bvh, beside, params), 1.0f, 1e-6f);
    }

    TEST(MATH_RayTracedShadows, OccluderBeyondMaxDistanceDoesNotShadow)
    {
        AZStd::vector<BvhTriangle> tris;
        AppendQuad(tris, -1.0f, -1.0f, 1.0f, 1.0f, 50.0f);
        RayTracingBvh bvh;
        bvh.Build(tris);

        ShadowSample sample{ Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f) };
        ShadowRayParams params;
        params.m_toLight = Vector3(0.0f, 0.0f, 1.0f);

        // Occluder at z = 50 is past a 10-unit ray -> lit.
        params.m_maxDistance = 10.0f;
        EXPECT_NEAR(AZ::ComputeShadowVisibility(bvh, sample, params), 1.0f, 1e-6f);

        // Same occluder now within reach -> shadowed.
        params.m_maxDistance = 100.0f;
        EXPECT_NEAR(AZ::ComputeShadowVisibility(bvh, sample, params), 0.0f, 1e-6f);
    }

    TEST(MATH_RayTracedShadows, NormalBiasPreventsSelfShadowOnTheOccluderItself)
    {
        // The surface point lies exactly on the occluder quad; without the normal-bias offset the
        // ray would immediately re-hit the same surface. The bias must lift the origin clear so the
        // point reads as lit.
        AZStd::vector<BvhTriangle> tris;
        AppendQuad(tris, -5.0f, -5.0f, 5.0f, 5.0f, 0.0f);
        RayTracingBvh bvh;
        bvh.Build(tris);

        ShadowSample onSurface{ Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f) };
        ShadowRayParams params;
        params.m_toLight = Vector3(0.0f, 0.0f, 1.0f);
        params.m_maxDistance = 100.0f;
        params.m_normalBias = 1e-2f;

        EXPECT_NEAR(AZ::ComputeShadowVisibility(bvh, onSurface, params), 1.0f, 1e-6f);
    }

    // Equivalence check: the batch helper must agree with an independent brute-force occlusion test
    // (linear scan of every triangle) for a random scene of occluders, surface points and a fixed
    // directional light.
    TEST(MATH_RayTracedShadows, BatchMatchesBruteForceOcclusion)
    {
        std::mt19937 rng(2024u);
        std::uniform_real_distribution<float> pos(-20.0f, 20.0f);
        std::uniform_real_distribution<float> size(1.0f, 4.0f);

        // Random axis-aligned quad occluders floating above the z = 0 ground plane.
        AZStd::vector<BvhTriangle> tris;
        for (int i = 0; i < 40; ++i)
        {
            const float cx = pos(rng);
            const float cy = pos(rng);
            const float z = 3.0f + size(rng);
            const float h = size(rng);
            AppendQuad(tris, cx - h, cy - h, cx + h, cy + h, z);
        }
        RayTracingBvh bvh;
        bvh.Build(tris);

        ShadowRayParams params;
        params.m_toLight = Vector3(0.0f, 0.0f, 1.0f);
        params.m_maxDistance = 100.0f;
        params.m_normalBias = 1e-3f;

        AZStd::vector<ShadowSample> samples;
        for (int i = 0; i < 600; ++i)
        {
            samples.push_back({ Vector3(pos(rng), pos(rng), 0.0f), Vector3(0.0f, 0.0f, 1.0f) });
        }

        AZStd::vector<float> visibility;
        AZ::ComputeDirectionalShadowVisibility(bvh, samples, params, visibility);
        ASSERT_EQ(visibility.size(), samples.size());

        const Vector3 dir = params.m_toLight.GetNormalizedSafe();
        size_t shadowed = 0;
        for (size_t i = 0; i < samples.size(); ++i)
        {
            const Vector3 origin = samples[i].m_position + samples[i].m_normal * params.m_normalBias;
            // Independent brute-force any-hit over all triangles.
            bool occluded = false;
            for (const BvhTriangle& tri : tris)
            {
                RayTracingBvh single;
                single.Build({ tri });
                if (single.IntersectAny(origin, dir, params.m_maxDistance))
                {
                    occluded = true;
                    break;
                }
            }
            const float expected = occluded ? 0.0f : 1.0f;
            EXPECT_NEAR(visibility[i], expected, 1e-6f) << "sample " << i;
            shadowed += occluded ? 1 : 0;
        }
        // Sanity: the random scene should produce a non-trivial mix of lit and shadowed samples.
        EXPECT_GT(shadowed, 0u);
        EXPECT_LT(shadowed, samples.size());
    }

    TEST(MATH_RayTracedShadows, SoftShadowProbeCountIsAQuarterOfTheSamplesButAtLeastTwo)
    {
        EXPECT_EQ(AZ::GetSoftShadowProbeCount(1), 1u);
        EXPECT_EQ(AZ::GetSoftShadowProbeCount(2), 2u);
        EXPECT_EQ(AZ::GetSoftShadowProbeCount(4), 2u);
        EXPECT_EQ(AZ::GetSoftShadowProbeCount(8), 2u);
        EXPECT_EQ(AZ::GetSoftShadowProbeCount(16), 4u);
        EXPECT_EQ(AZ::GetSoftShadowProbeCount(32), 8u);
    }

    TEST(MATH_RayTracedShadows, SoftShadowRayDirectionsStayInsideTheLightCone)
    {
        const float angularRadius = AZ::DegToRad(5.0f);
        const float tanRadius = std::tan(angularRadius);
        const uint32_t sampleCount = 16;

        // Include a light pointing down -z to cover the other branch of the orthonormal basis.
        const Vector3 lights[] = { Vector3(0.0f, 0.0f, 1.0f), Vector3(0.3f, -0.4f, 0.8f), Vector3(0.1f, 0.2f, -1.0f),
                                   Vector3(1.0f, 0.0f, 0.0f) };
        for (const Vector3& toLight : lights)
        {
            const Vector3 center = toLight.GetNormalized();
            float largestAngle = 0.0f;
            for (uint32_t i = 0; i < sampleCount; ++i)
            {
                const Vector3 dir = AZ::ComputeSoftShadowRayDirection(toLight, tanRadius, i, sampleCount, 1.3f);
                EXPECT_NEAR(dir.GetLength(), 1.0f, 1e-5f);
                const float angle = std::acos(AZ::GetClamp(dir.Dot(center), -1.0f, 1.0f));
                EXPECT_LE(angle, angularRadius + 1e-4f) << "sample " << i;
                largestAngle = AZ::GetMax(largestAngle, angle);
            }
            // The pattern must actually spread over the disk, not collapse onto the center.
            EXPECT_GT(largestAngle, 0.8f * angularRadius);
        }
    }

    TEST(MATH_RayTracedShadows, SoftShadowWithZeroRadiusOrOneSampleIsTheHardShadow)
    {
        std::mt19937 rng(77u);
        std::uniform_real_distribution<float> pos(-10.0f, 10.0f);
        AZStd::vector<BvhTriangle> tris;
        for (int i = 0; i < 20; ++i)
        {
            const float cx = pos(rng);
            const float cy = pos(rng);
            AppendQuad(tris, cx - 1.5f, cy - 1.5f, cx + 1.5f, cy + 1.5f, 4.0f);
        }
        RayTracingBvh bvh;
        bvh.Build(tris);

        ShadowRayParams params;
        params.m_toLight = Vector3(0.2f, 0.1f, 1.0f);
        params.m_maxDistance = 100.0f;

        SoftShadowParams oneSample;
        oneSample.m_angularRadius = AZ::DegToRad(3.0f);
        oneSample.m_sampleCount = 1;
        SoftShadowParams zeroRadius;
        zeroRadius.m_angularRadius = 0.0f;
        zeroRadius.m_sampleCount = 16;

        for (int i = 0; i < 300; ++i)
        {
            const ShadowSample sample{ Vector3(pos(rng), pos(rng), 0.0f), Vector3(0.0f, 0.0f, 1.0f) };
            const float hard = AZ::ComputeShadowVisibility(bvh, sample, params);
            EXPECT_EQ(AZ::ComputeSoftShadowVisibility(bvh, sample, params, oneSample), hard) << "sample " << i;
            EXPECT_EQ(AZ::ComputeSoftShadowVisibility(bvh, sample, params, zeroRadius), hard) << "sample " << i;
        }
    }

    // A half-plane occluder (x < 0) at height z, light straight up. Returns the visibility of
    // receivers on the ground along the x axis, from -range to +range.
    static AZStd::vector<float> SoftShadowEdgeProfile(float occluderHeight, float range, float step, const SoftShadowParams& soft)
    {
        AZStd::vector<BvhTriangle> tris;
        AppendQuad(tris, -100.0f, -100.0f, 0.0f, 100.0f, occluderHeight);
        RayTracingBvh bvh;
        bvh.Build(tris);

        ShadowRayParams params;
        params.m_toLight = Vector3(0.0f, 0.0f, 1.0f);
        params.m_maxDistance = 1000.0f;

        AZStd::vector<float> profile;
        for (float x = -range; x <= range + 0.5f * step; x += step)
        {
            const ShadowSample sample{ Vector3(x, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f) };
            profile.push_back(AZ::ComputeSoftShadowVisibility(bvh, sample, params, soft));
        }
        return profile;
    }

    TEST(MATH_RayTracedShadows, SoftShadowEdgeHasAMonotonicPenumbra)
    {
        SoftShadowParams soft;
        soft.m_angularRadius = AZ::DegToRad(2.0f);
        soft.m_sampleCount = 32;
        soft.m_adaptive = false;

        // Penumbra half-width = 5 * tan(2 deg) ~= 0.17 around x = 0.
        const AZStd::vector<float> profile = SoftShadowEdgeProfile(5.0f, 1.0f, 0.01f, soft);
        ASSERT_EQ(profile.size(), 201u);
        EXPECT_EQ(profile.front(), 0.0f); // umbra
        EXPECT_EQ(profile.back(), 1.0f);  // fully lit
        EXPECT_GT(profile[100], 0.0f);    // right at the edge: partially lit
        EXPECT_LT(profile[100], 1.0f);
        for (size_t i = 1; i < profile.size(); ++i)
        {
            EXPECT_GE(profile[i], profile[i - 1]) << "x index " << i;
        }
    }

    TEST(MATH_RayTracedShadows, SoftShadowPenumbraWidensWithOccluderDistance)
    {
        SoftShadowParams soft;
        soft.m_angularRadius = AZ::DegToRad(2.0f);
        soft.m_sampleCount = 32;
        soft.m_adaptive = false;

        auto penumbraSamples = [&soft](float occluderHeight)
        {
            const AZStd::vector<float> profile = SoftShadowEdgeProfile(occluderHeight, 2.0f, 0.01f, soft);
            return AZStd::count_if(profile.begin(), profile.end(), [](float v) { return v > 0.0f && v < 1.0f; });
        };

        // Contact hardening: the penumbra is ~2 * h * tan(radius) wide (0.14 at h = 2, 0.70 at h = 10).
        const auto nearWidth = penumbraSamples(2.0f);
        const auto farWidth = penumbraSamples(10.0f);
        EXPECT_GT(nearWidth, 0);
        EXPECT_GT(farWidth, 3 * nearWidth);
    }

    TEST(MATH_RayTracedShadows, AdaptiveSoftShadowMatchesFullSampling)
    {
        std::mt19937 rng(4242u);
        std::uniform_real_distribution<float> pos(-20.0f, 20.0f);
        std::uniform_real_distribution<float> size(1.0f, 4.0f);
        AZStd::vector<BvhTriangle> tris;
        for (int i = 0; i < 40; ++i)
        {
            const float cx = pos(rng);
            const float cy = pos(rng);
            const float h = size(rng);
            AppendQuad(tris, cx - h, cy - h, cx + h, cy + h, 3.0f + size(rng));
        }
        RayTracingBvh bvh;
        bvh.Build(tris);

        ShadowRayParams params;
        params.m_toLight = Vector3(0.1f, -0.2f, 1.0f);
        params.m_maxDistance = 100.0f;

        SoftShadowParams full;
        full.m_angularRadius = AZ::DegToRad(4.0f);
        full.m_sampleCount = 16;
        full.m_adaptive = false;
        SoftShadowParams adaptive = full;
        adaptive.m_adaptive = true;

        size_t penumbra = 0;
        size_t mismatches = 0;
        const size_t sampleCount = 2000;
        for (size_t i = 0; i < sampleCount; ++i)
        {
            const ShadowSample sample{ Vector3(pos(rng), pos(rng), 0.0f), Vector3(0.0f, 0.0f, 1.0f) };
            const float reference = AZ::ComputeSoftShadowVisibility(bvh, sample, params, full);
            const float fast = AZ::ComputeSoftShadowVisibility(bvh, sample, params, adaptive);
            if (reference == 0.0f || reference == 1.0f)
            {
                // Fully lit / fully shadowed: the probes all agree, so the early-out is exact.
                EXPECT_EQ(fast, reference) << "sample " << i;
            }
            else
            {
                ++penumbra;
                if (fast != reference)
                {
                    // Only possible when every probe agreed although some other ray disagreed.
                    EXPECT_TRUE(fast == 0.0f || fast == 1.0f) << "sample " << i;
                    ++mismatches;
                }
            }
        }
        EXPECT_GT(penumbra, 0u);
        EXPECT_LE(mismatches, sampleCount / 50) << mismatches << " of " << penumbra << " penumbra samples";
    }

    using AZ::LocalShadowLight;
    using AZ::WeightedShadowVisibility;

    static LocalShadowLight MakePointLight(const Vector3& position, float radius, float intensity = 100.0f)
    {
        LocalShadowLight light;
        light.m_position = position;
        light.m_radius = radius;
        light.m_intensity = intensity;
        return light;
    }

    TEST(MATH_RayTracedShadows, LocalLightIlluminanceFollowsInverseSquareAndRange)
    {
        LocalShadowLight light = MakePointLight(Vector3(0.0f, 0.0f, 0.0f), 0.0f, 100.0f);
        // Infinite range: plain inverse square.
        EXPECT_NEAR(AZ::ComputeLocalLightIlluminance(light, Vector3(0.0f, 0.0f, 2.0f)), 25.0f, 1e-3f);

        light.m_invAttenuationRadiusSquared = 1.0f / (10.0f * 10.0f);
        EXPECT_GT(AZ::ComputeLocalLightIlluminance(light, Vector3(0.0f, 0.0f, 2.0f)), 0.0f);
        EXPECT_LT(AZ::ComputeLocalLightIlluminance(light, Vector3(0.0f, 0.0f, 2.0f)), 25.0f);
        EXPECT_EQ(AZ::ComputeLocalLightIlluminance(light, Vector3(0.0f, 0.0f, 10.5f)), 0.0f);
    }

    TEST(MATH_RayTracedShadows, SpotLightIlluminanceIsZeroOutsideCone)
    {
        LocalShadowLight light = MakePointLight(Vector3(0.0f, 0.0f, 5.0f), 0.0f, 100.0f);
        light.m_spotDirection = Vector3(0.0f, 0.0f, -1.0f); // pointing down
        light.m_cosInnerConeAngle = std::cos(AZ::DegToRad(20.0f));
        light.m_cosOuterConeAngle = std::cos(AZ::DegToRad(30.0f));

        const float center = AZ::ComputeLocalLightIlluminance(light, Vector3(0.0f, 0.0f, 0.0f));
        EXPECT_NEAR(center, 4.0f, 1e-3f);
        // 5 * tan(25 deg) ~= 2.33: in the penumbra between the inner and outer cone.
        const float penumbra = AZ::ComputeLocalLightIlluminance(light, Vector3(2.33f, 0.0f, 0.0f));
        EXPECT_GT(penumbra, 0.0f);
        EXPECT_LT(penumbra, 4.0f / (1.0f + 2.33f * 2.33f / 25.0f));
        // 5 * tan(40 deg) ~= 4.2: outside the outer cone.
        EXPECT_EQ(AZ::ComputeLocalLightIlluminance(light, Vector3(4.2f, 0.0f, 0.0f)), 0.0f);
        // Behind the light.
        EXPECT_EQ(AZ::ComputeLocalLightIlluminance(light, Vector3(0.0f, 0.0f, 8.0f)), 0.0f);
    }

    TEST(MATH_RayTracedShadows, PointLightShadowStopsAtTheLight)
    {
        AZStd::vector<BvhTriangle> tris;
        AppendQuad(tris, -1.0f, -1.0f, 1.0f, 1.0f, 3.0f); // between receiver and light
        AppendQuad(tris, 9.0f, -1.0f, 11.0f, 1.0f, 8.0f);  // above a second light, behind it
        RayTracingBvh bvh;
        bvh.Build(tris);

        SoftShadowParams soft;
        const ShadowSample sample{ Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f) };
        const ShadowSample sample2{ Vector3(10.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f) };
        EXPECT_EQ(AZ::ComputeLocalLightShadowVisibility(bvh, sample, MakePointLight(Vector3(0.0f, 0.0f, 5.0f), 0.0f), 1e-3f, 0.0f, soft), 0.0f);
        EXPECT_EQ(AZ::ComputeLocalLightShadowVisibility(bvh, sample2, MakePointLight(Vector3(10.0f, 0.0f, 5.0f), 0.0f), 1e-3f, 0.0f, soft), 1.0f);
    }

    TEST(MATH_RayTracedShadows, LightClearanceIgnoresTheLightsOwnFixture)
    {
        // A lamp-shade-like quad 5 cm under the light.
        AZStd::vector<BvhTriangle> tris;
        AppendQuad(tris, -0.2f, -0.2f, 0.2f, 0.2f, 4.95f);
        RayTracingBvh bvh;
        bvh.Build(tris);

        SoftShadowParams soft;
        const ShadowSample sample{ Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f) };
        const LocalShadowLight light = MakePointLight(Vector3(0.0f, 0.0f, 5.0f), 0.0f);
        EXPECT_EQ(AZ::ComputeLocalLightShadowVisibility(bvh, sample, light, 1e-3f, 0.0f, soft), 0.0f);
        EXPECT_EQ(AZ::ComputeLocalLightShadowVisibility(bvh, sample, light, 1e-3f, 0.1f, soft), 1.0f);
    }

    TEST(MATH_RayTracedShadows, LocalLightSamplePointsLieOnTheLightDisk)
    {
        const Vector3 receiver(0.0f, 0.0f, 0.0f);
        LocalShadowLight sphere = MakePointLight(Vector3(1.0f, 2.0f, 5.0f), 0.5f);
        const Vector3 toReceiver = (receiver - sphere.m_position).GetNormalized();
        LocalShadowLight disk = sphere;
        disk.m_diskNormal = Vector3(0.0f, 0.0f, -1.0f);
        for (uint32_t i = 0; i < 16; ++i)
        {
            const Vector3 p = AZ::ComputeLocalLightSamplePoint(sphere, receiver, i, 16, 0.3f);
            EXPECT_LE(p.GetDistance(sphere.m_position), 0.5f + 1e-4f);
            EXPECT_NEAR((p - sphere.m_position).Dot(toReceiver), 0.0f, 1e-4f); // disk faces the receiver

            const Vector3 q = AZ::ComputeLocalLightSamplePoint(disk, receiver, i, 16, 0.3f);
            EXPECT_LE(q.GetDistance(disk.m_position), 0.5f + 1e-4f);
            EXPECT_NEAR(q.GetZ(), 5.0f, 1e-4f); // in the disk's plane
        }
        // Zero radius or one sample aims at the center.
        EXPECT_TRUE(AZ::ComputeLocalLightSamplePoint(sphere, receiver, 0, 1, 0.3f).IsClose(sphere.m_position));
        sphere.m_radius = 0.0f;
        EXPECT_TRUE(AZ::ComputeLocalLightSamplePoint(sphere, receiver, 5, 16, 0.3f).IsClose(sphere.m_position));
    }

    // Fraction of receivers along x in [-3, 3] that are partially (0 < v < 1) shadowed by a half-plane
    // occluder edge at x = 0, z = 2.5, from a point light at z = 5.
    static float LocalPenumbraFraction(float lightRadius)
    {
        AZStd::vector<BvhTriangle> tris;
        AppendQuad(tris, -100.0f, -100.0f, 0.0f, 100.0f, 2.5f);
        RayTracingBvh bvh;
        bvh.Build(tris);

        SoftShadowParams soft;
        soft.m_sampleCount = 32;
        soft.m_adaptive = false;
        const LocalShadowLight light = MakePointLight(Vector3(0.0f, 0.0f, 5.0f), lightRadius);
        int partial = 0;
        constexpr int Steps = 600;
        float previous = -1.0f;
        for (int i = 0; i <= Steps; ++i)
        {
            const float x = -3.0f + 6.0f * static_cast<float>(i) / Steps;
            const float v = AZ::ComputeLocalLightShadowVisibility(
                bvh, { Vector3(x, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f) }, light, 1e-3f, 0.0f, soft);
            EXPECT_GE(v + 1e-6f, previous); // visibility never decreases moving out of the shadow
            previous = v;
            partial += (v > 0.0f && v < 1.0f) ? 1 : 0;
        }
        return static_cast<float>(partial) / Steps;
    }

    TEST(MATH_RayTracedShadows, LocalLightPenumbraWidensWithLightRadius)
    {
        EXPECT_EQ(LocalPenumbraFraction(0.0f), 0.0f);
        const float small = LocalPenumbraFraction(0.25f);
        const float large = LocalPenumbraFraction(1.0f);
        EXPECT_GT(small, 0.0f);
        // Penumbra width = 2 * radius * (occluder-to-receiver / light-to-occluder) = 2 * radius.
        EXPECT_NEAR(large, 2.0f / 6.0f, 0.03f);
        EXPECT_GT(large, 3.0f * small);
    }

    TEST(MATH_RayTracedShadows, LocalLightAdaptiveMatchesFullSampling)
    {
        AZStd::vector<BvhTriangle> tris;
        AppendQuad(tris, -100.0f, -100.0f, 0.0f, 100.0f, 2.5f);
        RayTracingBvh bvh;
        bvh.Build(tris);

        SoftShadowParams full;
        full.m_sampleCount = 16;
        full.m_adaptive = false;
        SoftShadowParams adaptive = full;
        adaptive.m_adaptive = true;
        const LocalShadowLight light = MakePointLight(Vector3(0.0f, 0.0f, 5.0f), 0.5f);
        int mismatches = 0;
        for (int i = 0; i <= 400; ++i)
        {
            const ShadowSample sample{ Vector3(-2.0f + 4.0f * i / 400.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f) };
            const float a = AZ::ComputeLocalLightShadowVisibility(bvh, sample, light, 1e-3f, 0.0f, adaptive);
            const float b = AZ::ComputeLocalLightShadowVisibility(bvh, sample, light, 1e-3f, 0.0f, full);
            mismatches += std::fabs(a - b) > 1e-6f ? 1 : 0;
        }
        EXPECT_LE(mismatches, 8);
    }

    TEST(MATH_RayTracedShadows, CombineShadowVisibilityWeightsByIlluminance)
    {
        EXPECT_EQ(AZ::CombineShadowVisibility({}), 1.0f);
        EXPECT_EQ(AZ::CombineShadowVisibility({ { 0.0f, 0.0f } }), 1.0f);
        EXPECT_NEAR(AZ::CombineShadowVisibility({ { 5.0f, 0.3f } }), 0.3f, 1e-6f);
        // A bright sun fully lit plus a dim lamp fully shadowed: barely darker.
        EXPECT_NEAR(AZ::CombineShadowVisibility({ { 1000.0f, 1.0f }, { 10.0f, 0.0f } }), 1000.0f / 1010.0f, 1e-6f);
        EXPECT_NEAR(AZ::CombineShadowVisibility({ { 1.0f, 1.0f }, { 1.0f, 0.0f } }), 0.5f, 1e-6f);
    }

    TEST(MATH_RayTracedShadows, SelectShadowLightsKeepsBrightestNearCamera)
    {
        AZStd::vector<LocalShadowLight> lights;
        lights.push_back(MakePointLight(Vector3(1.0f, 0.0f, 0.0f), 0.0f, 10.0f));    // score 10
        lights.push_back(MakePointLight(Vector3(10.0f, 0.0f, 0.0f), 0.0f, 400.0f));  // score 4
        lights.push_back(MakePointLight(Vector3(2.0f, 0.0f, 0.0f), 0.0f, 80.0f));    // score 20
        lights.push_back(MakePointLight(Vector3(0.0f, 0.0f, 0.0f), 0.0f, 0.0f));     // off
        LocalShadowLight far = MakePointLight(Vector3(500.0f, 0.0f, 0.0f), 0.0f, 1.0e9f);
        far.m_invAttenuationRadiusSquared = 1.0f / (20.0f * 20.0f); // range ends 480 m from the camera
        lights.push_back(far);

        AZ::SelectShadowLights(lights, Vector3::CreateZero(), 100.0f, 2);
        ASSERT_EQ(lights.size(), 2u);
        EXPECT_EQ(lights[0].m_intensity, 80.0f);
        EXPECT_EQ(lights[1].m_intensity, 10.0f);
    }
} // namespace UnitTest
