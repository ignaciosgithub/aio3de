/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Math/RayTracingBvh.h>
#include <AzCore/std/limits.h>

namespace AZ
{
    namespace RayTracingBvhInternal
    {
        // Fixed traversal stack depth. A balanced binary BVH over N triangles is ~log2(N) deep;
        // 64 covers >2^64 triangles even for a fully degenerate (median-fallback) tree given the
        // depth guard in BuildNode. The same constant is mirrored in the GPU traversal shader.
        static constexpr int32_t StackSize = 64;
        static constexpr uint32_t MaxDepth = 60;

        // Slab test. Returns true if the ray (origin, invDir) enters the AABB before tMax (and exits
        // after 0). invDir may contain +/-inf for axis-aligned rays; the min/max ordering handles it.
        inline bool IntersectRayAabb(
            const Vector3& origin, const Vector3& invDir, const Vector3& aabbMin, const Vector3& aabbMax, float tMax)
        {
            float tNear = 0.0f;
            float tFar = tMax;
            for (int32_t axis = 0; axis < 3; ++axis)
            {
                const float inv = invDir.GetElement(axis);
                float t0 = (aabbMin.GetElement(axis) - origin.GetElement(axis)) * inv;
                float t1 = (aabbMax.GetElement(axis) - origin.GetElement(axis)) * inv;
                if (t0 > t1)
                {
                    const float tmp = t0;
                    t0 = t1;
                    t1 = tmp;
                }
                tNear = t0 > tNear ? t0 : tNear;
                tFar = t1 < tFar ? t1 : tFar;
                if (tNear > tFar)
                {
                    return false;
                }
            }
            return true;
        }

        // Moller-Trumbore ray/triangle, two-sided. Writes t/u/v and returns true on a hit in (0, tMax].
        inline bool IntersectRayTriangle(
            const Vector3& origin,
            const Vector3& direction,
            const BvhTriangle& tri,
            float tMax,
            float& outT,
            float& outU,
            float& outV)
        {
            constexpr float Epsilon = 1e-8f;
            const Vector3 edge1 = tri.m_v1 - tri.m_v0;
            const Vector3 edge2 = tri.m_v2 - tri.m_v0;
            const Vector3 pvec = direction.Cross(edge2);
            const float det = edge1.Dot(pvec);
            if (det > -Epsilon && det < Epsilon)
            {
                return false; // ray parallel to triangle
            }
            const float invDet = 1.0f / det;
            const Vector3 tvec = origin - tri.m_v0;
            const float u = tvec.Dot(pvec) * invDet;
            if (u < 0.0f || u > 1.0f)
            {
                return false;
            }
            const Vector3 qvec = tvec.Cross(edge1);
            const float v = direction.Dot(qvec) * invDet;
            if (v < 0.0f || (u + v) > 1.0f)
            {
                return false;
            }
            const float t = edge2.Dot(qvec) * invDet;
            if (t <= Epsilon || t > tMax)
            {
                return false;
            }
            outT = t;
            outU = u;
            outV = v;
            return true;
        }
    } // namespace RayTracingBvhInternal

    void RayTracingBvh::Build(const AZStd::vector<BvhTriangle>& triangles, uint32_t maxTrianglesPerLeaf)
    {
        m_nodes.clear();
        m_orderedTriangles.clear();
        m_primitiveIndices.clear();

        const uint32_t triangleCount = static_cast<uint32_t>(triangles.size());
        if (triangleCount == 0)
        {
            return;
        }
        maxTrianglesPerLeaf = maxTrianglesPerLeaf < 1 ? 1 : maxTrianglesPerLeaf;

        AZStd::vector<uint32_t> indices(triangleCount);
        for (uint32_t i = 0; i < triangleCount; ++i)
        {
            indices[i] = i;
        }

        AZStd::vector<Vector3> centroids(triangleCount);
        for (uint32_t i = 0; i < triangleCount; ++i)
        {
            const BvhTriangle& tri = triangles[i];
            centroids[i] = (tri.m_v0 + tri.m_v1 + tri.m_v2) / 3.0f;
        }

        m_nodes.reserve(triangleCount * 2);
        m_orderedTriangles.reserve(triangleCount);
        m_primitiveIndices.reserve(triangleCount);

        m_nodes.push_back(BvhNodePacked{});
        BuildNode(0, triangles, indices, centroids, 0, triangleCount, 0, maxTrianglesPerLeaf);
    }

    void RayTracingBvh::BuildNode(
        uint32_t nodeIndex,
        const AZStd::vector<BvhTriangle>& triangles,
        AZStd::vector<uint32_t>& indices,
        const AZStd::vector<Vector3>& centroids,
        uint32_t first,
        uint32_t count,
        uint32_t depth,
        uint32_t maxTrianglesPerLeaf)
    {
        // Bounds over the triangles in this range, plus centroid bounds for choosing the split axis.
        Vector3 aabbMin(AZStd::numeric_limits<float>::max());
        Vector3 aabbMax(-AZStd::numeric_limits<float>::max());
        Vector3 centroidMin(AZStd::numeric_limits<float>::max());
        Vector3 centroidMax(-AZStd::numeric_limits<float>::max());
        for (uint32_t i = first; i < first + count; ++i)
        {
            const BvhTriangle& tri = triangles[indices[i]];
            aabbMin = aabbMin.GetMin(tri.m_v0).GetMin(tri.m_v1).GetMin(tri.m_v2);
            aabbMax = aabbMax.GetMax(tri.m_v0).GetMax(tri.m_v1).GetMax(tri.m_v2);
            centroidMin = centroidMin.GetMin(centroids[indices[i]]);
            centroidMax = centroidMax.GetMax(centroids[indices[i]]);
        }

        m_nodes[nodeIndex].m_aabbMinX = aabbMin.GetX();
        m_nodes[nodeIndex].m_aabbMinY = aabbMin.GetY();
        m_nodes[nodeIndex].m_aabbMinZ = aabbMin.GetZ();
        m_nodes[nodeIndex].m_aabbMaxX = aabbMax.GetX();
        m_nodes[nodeIndex].m_aabbMaxY = aabbMax.GetY();
        m_nodes[nodeIndex].m_aabbMaxZ = aabbMax.GetZ();

        const bool makeLeaf = (count <= maxTrianglesPerLeaf) || (depth >= RayTracingBvhInternal::MaxDepth);
        if (makeLeaf)
        {
            m_nodes[nodeIndex].m_leftFirst = static_cast<uint32_t>(m_orderedTriangles.size());
            m_nodes[nodeIndex].m_triangleCount = count;
            for (uint32_t i = first; i < first + count; ++i)
            {
                m_orderedTriangles.push_back(triangles[indices[i]]);
                m_primitiveIndices.push_back(indices[i]);
            }
            return;
        }

        // Choose the longest centroid axis and split at its midpoint.
        const Vector3 extent = centroidMax - centroidMin;
        int32_t axis = 0;
        if (extent.GetElement(1) > extent.GetElement(axis))
        {
            axis = 1;
        }
        if (extent.GetElement(2) > extent.GetElement(axis))
        {
            axis = 2;
        }
        const float splitPos = (centroidMin.GetElement(axis) + centroidMax.GetElement(axis)) * 0.5f;

        // Partition indices[first, first+count) around splitPos by centroid axis.
        uint32_t midIndex = first;
        for (uint32_t i = first; i < first + count; ++i)
        {
            if (centroids[indices[i]].GetElement(axis) < splitPos)
            {
                const uint32_t tmp = indices[i];
                indices[i] = indices[midIndex];
                indices[midIndex] = tmp;
                ++midIndex;
            }
        }

        // Median fallback when the midpoint split puts everything on one side.
        if (midIndex == first || midIndex == first + count)
        {
            midIndex = first + count / 2;
        }

        const uint32_t leftCount = midIndex - first;
        const uint32_t rightCount = count - leftCount;

        // Reserve both children contiguously so the right child is always leftChild + 1.
        const uint32_t leftChild = static_cast<uint32_t>(m_nodes.size());
        m_nodes.push_back(BvhNodePacked{});
        m_nodes.push_back(BvhNodePacked{});
        m_nodes[nodeIndex].m_leftFirst = leftChild;
        m_nodes[nodeIndex].m_triangleCount = 0;

        BuildNode(leftChild, triangles, indices, centroids, first, leftCount, depth + 1, maxTrianglesPerLeaf);
        BuildNode(leftChild + 1, triangles, indices, centroids, midIndex, rightCount, depth + 1, maxTrianglesPerLeaf);
    }

    BvhRayHit RayTracingBvh::IntersectClosest(const Vector3& origin, const Vector3& direction, float tMax) const
    {
        using namespace RayTracingBvhInternal;
        BvhRayHit hit;
        hit.m_t = tMax;
        if (m_nodes.empty())
        {
            return hit;
        }

        const Vector3 invDir = direction.GetReciprocal();

        int32_t stack[StackSize];
        int32_t stackPtr = 0;
        stack[stackPtr++] = 0;

        while (stackPtr > 0)
        {
            const BvhNodePacked& node = m_nodes[stack[--stackPtr]];
            const Vector3 nodeMin(node.m_aabbMinX, node.m_aabbMinY, node.m_aabbMinZ);
            const Vector3 nodeMax(node.m_aabbMaxX, node.m_aabbMaxY, node.m_aabbMaxZ);
            if (!IntersectRayAabb(origin, invDir, nodeMin, nodeMax, hit.m_t))
            {
                continue;
            }

            if (node.m_triangleCount > 0)
            {
                for (uint32_t i = 0; i < node.m_triangleCount; ++i)
                {
                    const uint32_t slot = node.m_leftFirst + i;
                    float t, u, v;
                    if (IntersectRayTriangle(origin, direction, m_orderedTriangles[slot], hit.m_t, t, u, v))
                    {
                        hit.m_t = t;
                        hit.m_u = u;
                        hit.m_v = v;
                        hit.m_primitiveId = m_primitiveIndices[slot];
                        hit.m_hit = true;
                    }
                }
            }
            else if (stackPtr + 2 <= StackSize)
            {
                stack[stackPtr++] = node.m_leftFirst;
                stack[stackPtr++] = node.m_leftFirst + 1;
            }
        }

        return hit;
    }

    bool RayTracingBvh::IntersectAny(const Vector3& origin, const Vector3& direction, float tMax) const
    {
        using namespace RayTracingBvhInternal;
        if (m_nodes.empty())
        {
            return false;
        }

        const Vector3 invDir = direction.GetReciprocal();

        int32_t stack[StackSize];
        int32_t stackPtr = 0;
        stack[stackPtr++] = 0;

        while (stackPtr > 0)
        {
            const BvhNodePacked& node = m_nodes[stack[--stackPtr]];
            const Vector3 nodeMin(node.m_aabbMinX, node.m_aabbMinY, node.m_aabbMinZ);
            const Vector3 nodeMax(node.m_aabbMaxX, node.m_aabbMaxY, node.m_aabbMaxZ);
            if (!IntersectRayAabb(origin, invDir, nodeMin, nodeMax, tMax))
            {
                continue;
            }

            if (node.m_triangleCount > 0)
            {
                for (uint32_t i = 0; i < node.m_triangleCount; ++i)
                {
                    float t, u, v;
                    if (IntersectRayTriangle(origin, direction, m_orderedTriangles[node.m_leftFirst + i], tMax, t, u, v))
                    {
                        return true;
                    }
                }
            }
            else if (stackPtr + 2 <= StackSize)
            {
                stack[stackPtr++] = node.m_leftFirst;
                stack[stackPtr++] = node.m_leftFirst + 1;
            }
        }

        return false;
    }
} // namespace AZ
