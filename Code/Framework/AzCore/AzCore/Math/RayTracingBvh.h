/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Math/Vector3.h>
#include <AzCore/std/containers/vector.h>

namespace AZ
{
    //! A single triangle defined by three world-space vertices.
    struct BvhTriangle
    {
        Vector3 m_v0;
        Vector3 m_v1;
        Vector3 m_v2;
    };

    //! Packed BVH node, 32 bytes, laid out to be uploaded verbatim to a GPU
    //! StructuredBuffer and walked by the matching compute traversal shader.
    //!
    //! - Interior node: m_triangleCount == 0, m_leftFirst is the index of the left child
    //!   node; the right child is always m_leftFirst + 1 (children are stored contiguously).
    //! - Leaf node: m_triangleCount > 0, m_leftFirst is the offset into the ordered triangle
    //!   / primitive-index arrays of the first triangle in this leaf.
    struct BvhNodePacked
    {
        float m_aabbMinX, m_aabbMinY, m_aabbMinZ;
        uint32_t m_leftFirst;
        float m_aabbMaxX, m_aabbMaxY, m_aabbMaxZ;
        uint32_t m_triangleCount;
    };
    static_assert(sizeof(BvhNodePacked) == 32, "BvhNodePacked must stay 32 bytes to match the GPU layout");

    //! Closest-hit result of a ray query against a RayTracingBvh.
    struct BvhRayHit
    {
        float m_t = 0.0f;            //!< Distance along the ray to the hit (valid only when m_hit).
        float m_u = 0.0f;            //!< Barycentric coordinate (Moller-Trumbore).
        float m_v = 0.0f;            //!< Barycentric coordinate (Moller-Trumbore).
        uint32_t m_primitiveId = 0;  //!< Index of the hit triangle in the original Build() input.
        bool m_hit = false;          //!< True if the ray hit a triangle within tMax.
    };

    //! Portable, hardware-agnostic bounding volume hierarchy for ray/triangle queries.
    //!
    //! The builder produces a binary BVH (median/midpoint split on the longest centroid axis)
    //! stored as a flat array of BvhNodePacked plus reordered triangle and primitive-index
    //! arrays. The CPU IntersectClosest() is the reference implementation; the same node layout
    //! and the same slab / Moller-Trumbore tests are mirrored by the GPU compute traversal shader
    //! (RayTracingBvhTraverse.azsl) so the two agree without depending on any hardware ray-tracing
    //! API (DXR / VK_KHR_ray_tracing) or RT cores.
    class AZCORE_API RayTracingBvh
    {
    public:
        RayTracingBvh() = default;

        //! Builds the BVH over \p triangles. Clears any previously built data. Safe to call with
        //! an empty list (queries then always miss).
        //! @param maxTrianglesPerLeaf upper bound on triangles stored in a leaf node (>= 1).
        void Build(const AZStd::vector<BvhTriangle>& triangles, uint32_t maxTrianglesPerLeaf = 2);

        //! Returns the closest triangle hit by the ray (origin + t * direction, 0 < t <= tMax).
        //! \p direction need not be normalized; m_t is expressed in units of \p direction length.
        BvhRayHit IntersectClosest(const Vector3& origin, const Vector3& direction, float tMax) const;

        //! Returns true if any triangle is hit within (0, tMax]; cheaper than IntersectClosest as
        //! it stops at the first hit (used for shadow/occlusion rays).
        bool IntersectAny(const Vector3& origin, const Vector3& direction, float tMax) const;

        //! Flat node array (root at index 0). Empty when no triangles were built.
        const AZStd::vector<BvhNodePacked>& GetNodes() const { return m_nodes; }

        //! Triangles reordered into BVH leaf order; index a leaf's triangles as
        //! [m_leftFirst, m_leftFirst + m_triangleCount). Upload these (not the Build() input) to the GPU.
        const AZStd::vector<BvhTriangle>& GetOrderedTriangles() const { return m_orderedTriangles; }

        //! Maps an ordered-triangle slot back to its index in the original Build() input, for
        //! reporting primitiveId. Same length and order as GetOrderedTriangles().
        const AZStd::vector<uint32_t>& GetPrimitiveIndices() const { return m_primitiveIndices; }

        //! Number of nodes / ordered triangles, convenience for sizing GPU buffers.
        uint32_t GetNodeCount() const { return static_cast<uint32_t>(m_nodes.size()); }
        uint32_t GetTriangleCount() const { return static_cast<uint32_t>(m_orderedTriangles.size()); }

    private:
        void BuildNode(
            uint32_t nodeIndex,
            const AZStd::vector<BvhTriangle>& triangles,
            AZStd::vector<uint32_t>& indices,
            const AZStd::vector<Vector3>& centroids,
            uint32_t first,
            uint32_t count,
            uint32_t depth,
            uint32_t maxTrianglesPerLeaf);

        AZStd::vector<BvhNodePacked> m_nodes;
        AZStd::vector<BvhTriangle> m_orderedTriangles;
        AZStd::vector<uint32_t> m_primitiveIndices;
    };
} // namespace AZ
