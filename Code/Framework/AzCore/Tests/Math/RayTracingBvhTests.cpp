/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Math/RayTracingBvh.h>
#include <AzCore/UnitTest/TestTypes.h>
#include <AzCore/std/limits.h>
#include <random>

namespace UnitTest
{
    using AZ::BvhRayHit;
    using AZ::BvhTriangle;
    using AZ::RayTracingBvh;
    using AZ::Vector3;

    // Reference Moller-Trumbore identical to the one inside RayTracingBvh.cpp, so the brute-force
    // baseline and the BVH share the exact same triangle math. The only thing under test is then
    // whether the BVH acceleration structure returns the same closest hit as a linear scan.
    static bool RefIntersect(
        const Vector3& origin, const Vector3& dir, const BvhTriangle& tri, float tMax, float& outT)
    {
        constexpr float Epsilon = 1e-8f;
        const Vector3 e1 = tri.m_v1 - tri.m_v0;
        const Vector3 e2 = tri.m_v2 - tri.m_v0;
        const Vector3 pvec = dir.Cross(e2);
        const float det = e1.Dot(pvec);
        if (det > -Epsilon && det < Epsilon)
        {
            return false;
        }
        const float invDet = 1.0f / det;
        const Vector3 tvec = origin - tri.m_v0;
        const float u = tvec.Dot(pvec) * invDet;
        if (u < 0.0f || u > 1.0f)
        {
            return false;
        }
        const Vector3 qvec = tvec.Cross(e1);
        const float v = dir.Dot(qvec) * invDet;
        if (v < 0.0f || (u + v) > 1.0f)
        {
            return false;
        }
        const float t = e2.Dot(qvec) * invDet;
        if (t <= Epsilon || t > tMax)
        {
            return false;
        }
        outT = t;
        return true;
    }

    // Linear-scan closest hit, the ground-truth the BVH must reproduce.
    static BvhRayHit BruteForceClosest(
        const AZStd::vector<BvhTriangle>& tris, const Vector3& origin, const Vector3& dir, float tMax)
    {
        BvhRayHit hit;
        hit.m_t = tMax;
        for (uint32_t i = 0; i < tris.size(); ++i)
        {
            float t;
            if (RefIntersect(origin, dir, tris[i], hit.m_t, t))
            {
                hit.m_t = t;
                hit.m_primitiveId = i;
                hit.m_hit = true;
            }
        }
        return hit;
    }

    TEST(MATH_RayTracingBvh, EmptyBvhAlwaysMisses)
    {
        RayTracingBvh bvh;
        bvh.Build({});
        EXPECT_EQ(bvh.GetNodeCount(), 0u);
        EXPECT_EQ(bvh.GetTriangleCount(), 0u);
        const BvhRayHit hit = bvh.IntersectClosest(Vector3(0.0f), Vector3(0.0f, 1.0f, 0.0f), 1000.0f);
        EXPECT_FALSE(hit.m_hit);
        EXPECT_FALSE(bvh.IntersectAny(Vector3(0.0f), Vector3(0.0f, 1.0f, 0.0f), 1000.0f));
    }

    TEST(MATH_RayTracingBvh, SingleTriangleHitReportsBarycentricsAndDistance)
    {
        // Triangle in the z = 5 plane; ray straight down +z through its centroid.
        AZStd::vector<BvhTriangle> tris = { { Vector3(0.0f, 0.0f, 5.0f), Vector3(1.0f, 0.0f, 5.0f), Vector3(0.0f, 1.0f, 5.0f) } };
        RayTracingBvh bvh;
        bvh.Build(tris);

        const BvhRayHit hit = bvh.IntersectClosest(Vector3(0.25f, 0.25f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), 100.0f);
        ASSERT_TRUE(hit.m_hit);
        EXPECT_NEAR(hit.m_t, 5.0f, 1e-4f);
        EXPECT_EQ(hit.m_primitiveId, 0u);
        EXPECT_NEAR(hit.m_u, 0.25f, 1e-4f);
        EXPECT_NEAR(hit.m_v, 0.25f, 1e-4f);
    }

    TEST(MATH_RayTracingBvh, RayMissingTriangleReturnsNoHit)
    {
        AZStd::vector<BvhTriangle> tris = { { Vector3(0.0f, 0.0f, 5.0f), Vector3(1.0f, 0.0f, 5.0f), Vector3(0.0f, 1.0f, 5.0f) } };
        RayTracingBvh bvh;
        bvh.Build(tris);

        // Passes well outside the triangle.
        EXPECT_FALSE(bvh.IntersectClosest(Vector3(5.0f, 5.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), 100.0f).m_hit);
        // Triangle is behind the ray origin.
        EXPECT_FALSE(bvh.IntersectClosest(Vector3(0.25f, 0.25f, 10.0f), Vector3(0.0f, 0.0f, 1.0f), 100.0f).m_hit);
        // Hit exists but lies beyond tMax.
        EXPECT_FALSE(bvh.IntersectClosest(Vector3(0.25f, 0.25f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), 4.0f).m_hit);
    }

    TEST(MATH_RayTracingBvh, ClosestHitPicksNearestOfStackedTriangles)
    {
        // Three parallel triangles at increasing z; expect the nearest (z = 2) along a +z ray.
        AZStd::vector<BvhTriangle> tris;
        for (float z : { 8.0f, 2.0f, 5.0f })
        {
            tris.push_back({ Vector3(-1.0f, -1.0f, z), Vector3(3.0f, -1.0f, z), Vector3(-1.0f, 3.0f, z) });
        }
        RayTracingBvh bvh;
        bvh.Build(tris);

        const BvhRayHit hit = bvh.IntersectClosest(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), 100.0f);
        ASSERT_TRUE(hit.m_hit);
        EXPECT_NEAR(hit.m_t, 2.0f, 1e-4f);
        EXPECT_EQ(hit.m_primitiveId, 1u); // the z = 2 triangle was input index 1
    }

    // Generates a cloud of random triangles and asserts the BVH closest hit matches a brute-force
    // linear scan for many random rays, across several leaf sizes. This is the core structural test.
    TEST(MATH_RayTracingBvh, MatchesBruteForceOverRandomScene)
    {
        std::mt19937 rng(1234u);
        std::uniform_real_distribution<float> pos(-50.0f, 50.0f);
        std::uniform_real_distribution<float> size(0.5f, 6.0f);
        std::uniform_real_distribution<float> dirDist(-1.0f, 1.0f);

        constexpr uint32_t TriangleCount = 400;
        AZStd::vector<BvhTriangle> tris;
        tris.reserve(TriangleCount);
        for (uint32_t i = 0; i < TriangleCount; ++i)
        {
            const Vector3 base(pos(rng), pos(rng), pos(rng));
            tris.push_back(
                { base,
                  base + Vector3(size(rng), dirDist(rng), dirDist(rng)),
                  base + Vector3(dirDist(rng), size(rng), dirDist(rng)) });
        }

        for (uint32_t maxLeaf : { 1u, 2u, 4u, 8u })
        {
            RayTracingBvh bvh;
            bvh.Build(tris, maxLeaf);

            // Structure invariants.
            EXPECT_EQ(bvh.GetTriangleCount(), TriangleCount);
            EXPECT_EQ(bvh.GetPrimitiveIndices().size(), TriangleCount);
            // Primitive indices must be a permutation of [0, TriangleCount).
            AZStd::vector<uint8_t> seen(TriangleCount, 0);
            for (uint32_t idx : bvh.GetPrimitiveIndices())
            {
                ASSERT_LT(idx, TriangleCount);
                seen[idx] = 1;
            }
            for (uint32_t i = 0; i < TriangleCount; ++i)
            {
                EXPECT_EQ(seen[i], 1) << "primitive " << i << " missing for maxLeaf " << maxLeaf;
            }

            constexpr uint32_t RayCount = 500;
            uint32_t hits = 0;
            for (uint32_t r = 0; r < RayCount; ++r)
            {
                const Vector3 origin(pos(rng), pos(rng), pos(rng));
                Vector3 dir(dirDist(rng), dirDist(rng), dirDist(rng));
                if (dir.GetLength() < 1e-3f)
                {
                    dir = Vector3(0.0f, 0.0f, 1.0f);
                }
                constexpr float TMax = 500.0f;

                const BvhRayHit bvhHit = bvh.IntersectClosest(origin, dir, TMax);
                const BvhRayHit refHit = BruteForceClosest(tris, origin, dir, TMax);

                ASSERT_EQ(bvhHit.m_hit, refHit.m_hit) << "hit mismatch ray " << r << " maxLeaf " << maxLeaf;
                if (refHit.m_hit)
                {
                    ++hits;
                    EXPECT_NEAR(bvhHit.m_t, refHit.m_t, 1e-3f) << "t mismatch ray " << r << " maxLeaf " << maxLeaf;
                    EXPECT_EQ(bvhHit.m_primitiveId, refHit.m_primitiveId)
                        << "primId mismatch ray " << r << " maxLeaf " << maxLeaf;
                }
                EXPECT_EQ(bvhHit.m_hit, bvh.IntersectAny(origin, dir, TMax));
            }
            // Sanity: the random scene/rays should produce a non-trivial number of hits.
            EXPECT_GT(hits, 0u) << "expected some hits for maxLeaf " << maxLeaf;
        }
    }

    TEST(MATH_RayTracingBvh, ChildNodesAreContiguousAndLeavesCoverAllTriangles)
    {
        std::mt19937 rng(99u);
        std::uniform_real_distribution<float> pos(-10.0f, 10.0f);
        AZStd::vector<BvhTriangle> tris;
        for (uint32_t i = 0; i < 64; ++i)
        {
            const Vector3 b(pos(rng), pos(rng), pos(rng));
            tris.push_back({ b, b + Vector3(1.0f, 0.0f, 0.0f), b + Vector3(0.0f, 1.0f, 0.0f) });
        }
        RayTracingBvh bvh;
        bvh.Build(tris, 2);

        const auto& nodes = bvh.GetNodes();
        ASSERT_FALSE(nodes.empty());
        uint32_t leafTriangleSum = 0;
        for (const auto& node : nodes)
        {
            if (node.m_triangleCount == 0)
            {
                // Interior: both children must be valid, distinct, contiguous indices.
                EXPECT_LT(node.m_leftFirst + 1, nodes.size());
            }
            else
            {
                // Leaf: triangle range must lie within the ordered-triangle array.
                EXPECT_LE(node.m_leftFirst + node.m_triangleCount, bvh.GetTriangleCount());
                leafTriangleSum += node.m_triangleCount;
            }
        }
        EXPECT_EQ(leafTriangleSum, bvh.GetTriangleCount());
    }
} // namespace UnitTest
