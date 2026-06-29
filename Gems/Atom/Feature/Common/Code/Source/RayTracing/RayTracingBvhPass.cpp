/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <RayTracing/RayTracingBvhPass.h>

#include <AzCore/Math/Vector4.h>

#include <Atom/RPI.Public/Buffer/BufferSystemInterface.h>
#include <Atom/RPI.Public/Buffer/Buffer.h>

namespace AZ
{
    namespace Render
    {
        RPI::Ptr<RayTracingBvhPass> RayTracingBvhPass::Create(const RPI::PassDescriptor& descriptor)
        {
            return aznew RayTracingBvhPass(descriptor);
        }

        RayTracingBvhPass::RayTracingBvhPass(const RPI::PassDescriptor& descriptor)
            : RPI::ComputePass(descriptor)
        {
        }

        void RayTracingBvhPass::SetGeometry(const AZStd::vector<AZ::BvhTriangle>& triangles)
        {
            m_bvh.Build(triangles);

            // Pack the ordered triangles as 3 float4 per triangle (xyz used), matching the shader's
            // m_triangleVertices indexing (3 * slot + 0/1/2).
            const AZStd::vector<AZ::BvhTriangle>& ordered = m_bvh.GetOrderedTriangles();
            m_triangleVertices.clear();
            m_triangleVertices.reserve(ordered.size() * 3);
            for (const AZ::BvhTriangle& tri : ordered)
            {
                m_triangleVertices.emplace_back(tri.m_v0.GetX(), tri.m_v0.GetY(), tri.m_v0.GetZ(), 0.0f);
                m_triangleVertices.emplace_back(tri.m_v1.GetX(), tri.m_v1.GetY(), tri.m_v1.GetZ(), 0.0f);
                m_triangleVertices.emplace_back(tri.m_v2.GetX(), tri.m_v2.GetY(), tri.m_v2.GetZ(), 0.0f);
            }

            m_buffersDirty = true;
        }

        void RayTracingBvhPass::SetRays(const AZStd::vector<Ray>& rays)
        {
            m_rays = rays;
            m_buffersDirty = true;
        }

        void RayTracingBvhPass::CreateBuffers()
        {
            const auto& nodes = m_bvh.GetNodes();
            const auto& primitiveIndices = m_bvh.GetPrimitiveIndices();

            const uint32_t nodeCount = AZStd::max<uint32_t>(1, static_cast<uint32_t>(nodes.size()));
            const uint32_t vertexCount = AZStd::max<uint32_t>(1, static_cast<uint32_t>(m_triangleVertices.size()));
            const uint32_t primCount = AZStd::max<uint32_t>(1, static_cast<uint32_t>(primitiveIndices.size()));
            const uint32_t rayCount = AZStd::max<uint32_t>(1, static_cast<uint32_t>(m_rays.size()));

            // Input: BVH nodes (AZ::BvhNodePacked, 32 bytes, mirrors the shader BvhNode).
            {
                RPI::CommonBufferDescriptor desc;
                desc.m_poolType = RPI::CommonBufferPoolType::ReadOnly;
                desc.m_bufferName = "RayTracingBvh.Nodes";
                desc.m_elementSize = sizeof(AZ::BvhNodePacked);
                desc.m_byteCount = static_cast<uint64_t>(desc.m_elementSize) * nodeCount;
                desc.m_bufferData = nodes.empty() ? nullptr : nodes.data();
                m_nodesBuffer = RPI::BufferSystemInterface::Get()->CreateBufferFromCommonPool(desc);
            }

            // Input: ordered triangle vertices, 3 float4 per triangle.
            {
                RPI::CommonBufferDescriptor desc;
                desc.m_poolType = RPI::CommonBufferPoolType::ReadOnly;
                desc.m_bufferName = "RayTracingBvh.TriangleVertices";
                desc.m_elementSize = sizeof(AZ::Vector4);
                desc.m_byteCount = static_cast<uint64_t>(desc.m_elementSize) * vertexCount;
                desc.m_bufferData = m_triangleVertices.empty() ? nullptr : m_triangleVertices.data();
                m_triangleVerticesBuffer = RPI::BufferSystemInterface::Get()->CreateBufferFromCommonPool(desc);
            }

            // Input: ordered-slot -> original primitive id.
            {
                RPI::CommonBufferDescriptor desc;
                desc.m_poolType = RPI::CommonBufferPoolType::ReadOnly;
                desc.m_bufferName = "RayTracingBvh.PrimitiveIndices";
                desc.m_elementSize = sizeof(uint32_t);
                desc.m_byteCount = static_cast<uint64_t>(desc.m_elementSize) * primCount;
                desc.m_bufferData = primitiveIndices.empty() ? nullptr : primitiveIndices.data();
                m_primitiveIndicesBuffer = RPI::BufferSystemInterface::Get()->CreateBufferFromCommonPool(desc);
            }

            // Input: rays to trace (32 bytes each, matches the shader BvhRay).
            {
                RPI::CommonBufferDescriptor desc;
                desc.m_poolType = RPI::CommonBufferPoolType::ReadOnly;
                desc.m_bufferName = "RayTracingBvh.Rays";
                desc.m_elementSize = sizeof(Ray);
                desc.m_byteCount = static_cast<uint64_t>(desc.m_elementSize) * rayCount;
                desc.m_bufferData = m_rays.empty() ? nullptr : m_rays.data();
                m_raysBuffer = RPI::BufferSystemInterface::Get()->CreateBufferFromCommonPool(desc);
            }

            // Output: one hit per ray.
            {
                RPI::CommonBufferDescriptor desc;
                desc.m_poolType = RPI::CommonBufferPoolType::ReadWrite;
                desc.m_bufferName = "RayTracingBvh.Hits";
                desc.m_elementSize = HitElementSize;
                desc.m_byteCount = static_cast<uint64_t>(desc.m_elementSize) * rayCount;
                desc.m_bufferData = nullptr;
                m_hitsBuffer = RPI::BufferSystemInterface::Get()->CreateBufferFromCommonPool(desc);
            }

            m_buffersDirty = false;
        }

        void RayTracingBvhPass::BuildInternal()
        {
            if (!m_hitsBuffer || m_buffersDirty)
            {
                CreateBuffers();
            }

            AttachBufferToSlot(NodesSlotName, m_nodesBuffer);
            AttachBufferToSlot(TriangleVerticesSlotName, m_triangleVerticesBuffer);
            AttachBufferToSlot(PrimitiveIndicesSlotName, m_primitiveIndicesBuffer);
            AttachBufferToSlot(RaysSlotName, m_raysBuffer);
            AttachBufferToSlot(HitsSlotName, m_hitsBuffer);

            ComputePass::BuildInternal();
        }

        void RayTracingBvhPass::FrameBeginInternal(FramePrepareParams params)
        {
            const uint32_t rayCount = static_cast<uint32_t>(m_rays.size());

            if (auto* srg = m_shaderResourceGroup.get())
            {
                srg->SetConstant(m_rayCountIndex, rayCount);
                srg->SetConstant(m_nodeCountIndex, m_bvh.GetNodeCount());
            }

            // One thread per ray.
            SetTargetThreadCounts(AZStd::max<uint32_t>(1, rayCount), 1, 1);

            ComputePass::FrameBeginInternal(params);
        }
    } // namespace Render
} // namespace AZ
