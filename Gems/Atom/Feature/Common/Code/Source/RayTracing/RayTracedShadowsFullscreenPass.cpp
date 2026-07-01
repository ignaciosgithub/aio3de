/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <RayTracing/RayTracedShadowsFullscreenPass.h>

#include <Atom/RPI.Public/Buffer/BufferSystemInterface.h>
#include <Atom/RPI.Public/Buffer/Buffer.h>

namespace AZ
{
    namespace Render
    {
        RPI::Ptr<RayTracedShadowsFullscreenPass> RayTracedShadowsFullscreenPass::Create(const RPI::PassDescriptor& descriptor)
        {
            return aznew RayTracedShadowsFullscreenPass(descriptor);
        }

        RayTracedShadowsFullscreenPass::RayTracedShadowsFullscreenPass(const RPI::PassDescriptor& descriptor)
            : RPI::FullscreenTrianglePass(descriptor)
        {
        }

        void RayTracedShadowsFullscreenPass::SetOccluderGeometry(const AZStd::vector<AZ::BvhTriangle>& triangles)
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

        void RayTracedShadowsFullscreenPass::SetShadowParams(const AZ::ShadowRayParams& params)
        {
            m_params = params;
        }

        void RayTracedShadowsFullscreenPass::CreateBuffers()
        {
            const auto& nodes = m_bvh.GetNodes();

            const uint32_t nodeCount = AZStd::max<uint32_t>(1, static_cast<uint32_t>(nodes.size()));
            const uint32_t vertexCount = AZStd::max<uint32_t>(1, static_cast<uint32_t>(m_triangleVertices.size()));

            // Input: BVH nodes (AZ::BvhNodePacked, 32 bytes, mirrors the shader BvhNode).
            {
                RPI::CommonBufferDescriptor desc;
                desc.m_poolType = RPI::CommonBufferPoolType::ReadOnly;
                desc.m_bufferName = "RayTracedShadowsFullscreen.Nodes";
                desc.m_elementSize = sizeof(AZ::BvhNodePacked);
                desc.m_byteCount = static_cast<uint64_t>(desc.m_elementSize) * nodeCount;
                desc.m_bufferData = nodes.empty() ? nullptr : nodes.data();
                m_nodesBuffer = RPI::BufferSystemInterface::Get()->CreateBufferFromCommonPool(desc);
            }

            // Input: ordered occluder triangle vertices, 3 float4 per triangle.
            {
                RPI::CommonBufferDescriptor desc;
                desc.m_poolType = RPI::CommonBufferPoolType::ReadOnly;
                desc.m_bufferName = "RayTracedShadowsFullscreen.TriangleVertices";
                desc.m_elementSize = sizeof(AZ::Vector4);
                desc.m_byteCount = static_cast<uint64_t>(desc.m_elementSize) * vertexCount;
                desc.m_bufferData = m_triangleVertices.empty() ? nullptr : m_triangleVertices.data();
                m_triangleVerticesBuffer = RPI::BufferSystemInterface::Get()->CreateBufferFromCommonPool(desc);
            }

            m_buffersDirty = false;
        }

        void RayTracedShadowsFullscreenPass::FrameBeginInternal(FramePrepareParams params)
        {
            if (!m_nodesBuffer || m_buffersDirty)
            {
                CreateBuffers();
            }

            auto* srg = m_shaderResourceGroup.get();
            if (srg && m_nodesBuffer && m_triangleVerticesBuffer)
            {
                srg->SetBufferView(m_nodesBufferIndex, m_nodesBuffer->GetBufferView());
                srg->SetBufferView(m_triangleVerticesBufferIndex, m_triangleVerticesBuffer->GetBufferView());
                srg->SetConstant(m_toLightIndex, m_params.m_toLight.GetNormalizedSafe());
                srg->SetConstant(m_maxDistanceIndex, m_params.m_maxDistance);
                srg->SetConstant(m_rayBiasIndex, m_params.m_normalBias);
                srg->SetConstant(m_shadowFactorIndex, m_shadowFactor);
                srg->SetConstant(m_nodeCountIndex, m_bvh.GetNodeCount());
            }

            FullscreenTrianglePass::FrameBeginInternal(params);
        }
    } // namespace Render
} // namespace AZ
