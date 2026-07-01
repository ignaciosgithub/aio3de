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
#include <random>

namespace UnitTest
{
    using AZ::BvhTriangle;
    using AZ::RayTracingBvh;
    using AZ::ShadowRayParams;
    using AZ::ShadowSample;
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
} // namespace UnitTest
