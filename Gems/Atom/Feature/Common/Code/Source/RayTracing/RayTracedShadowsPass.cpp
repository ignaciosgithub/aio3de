/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <RayTracing/RayTracedShadowsPass.h>

#include <Atom/RPI.Public/Buffer/BufferSystemInterface.h>
#include <Atom/RPI.Public/Buffer/Buffer.h>

namespace AZ
{
    namespace Render
    {
        RPI::Ptr<RayTracedShadowsPass> RayTracedShadowsPass::Create(const RPI::PassDescriptor& descriptor)
        {
            return aznew RayTracedShadowsPass(descriptor);
        }

        RayTracedShadowsPass::RayTracedShadowsPass(const RPI::PassDescriptor& descriptor)
            : RPI::ComputePass(descriptor)
        {
        }

        void RayTracedShadowsPass::SetOccluderGeometry(const AZStd::vector<AZ::BvhTriangle>& triangles)
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

        void RayTracedShadowsPass::SetSamples(const AZStd::vector<AZ::ShadowSample>& samples)
        {
            m_samples.clear();
            m_samples.reserve(samples.size());
            for (const AZ::ShadowSample& s : samples)
            {
                SampleGpu gpu;
                gpu.m_position = { { s.m_position.GetX(), s.m_position.GetY(), s.m_position.GetZ(), 0.0f } };
                gpu.m_normal = { { s.m_normal.GetX(), s.m_normal.GetY(), s.m_normal.GetZ(), 0.0f } };
                m_samples.push_back(gpu);
            }
            m_buffersDirty = true;
        }

        void RayTracedShadowsPass::SetShadowParams(const AZ::ShadowRayParams& params)
        {
            m_params = params;
        }

        void RayTracedShadowsPass::CreateBuffers()
        {
            const auto& nodes = m_bvh.GetNodes();

            const uint32_t nodeCount = AZStd::max<uint32_t>(1, static_cast<uint32_t>(nodes.size()));
            const uint32_t vertexCount = AZStd::max<uint32_t>(1, static_cast<uint32_t>(m_triangleVertices.size()));
            const uint32_t sampleCount = AZStd::max<uint32_t>(1, static_cast<uint32_t>(m_samples.size()));

            // Input: BVH nodes (AZ::BvhNodePacked, 32 bytes, mirrors the shader BvhNode).
            {
                RPI::CommonBufferDescriptor desc;
                desc.m_poolType = RPI::CommonBufferPoolType::ReadOnly;
                desc.m_bufferName = "RayTracedShadows.Nodes";
                desc.m_elementSize = sizeof(AZ::BvhNodePacked);
                desc.m_byteCount = static_cast<uint64_t>(desc.m_elementSize) * nodeCount;
                desc.m_bufferData = nodes.empty() ? nullptr : nodes.data();
                m_nodesBuffer = RPI::BufferSystemInterface::Get()->CreateBufferFromCommonPool(desc);
            }

            // Input: ordered occluder triangle vertices, 3 float4 per triangle.
            {
                RPI::CommonBufferDescriptor desc;
                desc.m_poolType = RPI::CommonBufferPoolType::ReadOnly;
                desc.m_bufferName = "RayTracedShadows.TriangleVertices";
                desc.m_elementSize = sizeof(AZ::Vector4);
                desc.m_byteCount = static_cast<uint64_t>(desc.m_elementSize) * vertexCount;
                desc.m_bufferData = m_triangleVertices.empty() ? nullptr : m_triangleVertices.data();
                m_triangleVerticesBuffer = RPI::BufferSystemInterface::Get()->CreateBufferFromCommonPool(desc);
            }

            // Input: shaded surface samples (32 bytes each, matches the shader ShadowSampleGpu).
            {
                RPI::CommonBufferDescriptor desc;
                desc.m_poolType = RPI::CommonBufferPoolType::ReadOnly;
                desc.m_bufferName = "RayTracedShadows.Samples";
                desc.m_elementSize = sizeof(SampleGpu);
                desc.m_byteCount = static_cast<uint64_t>(desc.m_elementSize) * sampleCount;
                desc.m_bufferData = m_samples.empty() ? nullptr : m_samples.data();
                m_samplesBuffer = RPI::BufferSystemInterface::Get()->CreateBufferFromCommonPool(desc);
            }

            // Output: one visibility float per sample.
            {
                RPI::CommonBufferDescriptor desc;
                desc.m_poolType = RPI::CommonBufferPoolType::ReadWrite;
                desc.m_bufferName = "RayTracedShadows.Visibility";
                desc.m_elementSize = sizeof(float);
                desc.m_byteCount = static_cast<uint64_t>(desc.m_elementSize) * sampleCount;
                desc.m_bufferData = nullptr;
                m_visibilityBuffer = RPI::BufferSystemInterface::Get()->CreateBufferFromCommonPool(desc);
            }

            m_buffersDirty = false;
        }

        void RayTracedShadowsPass::BuildInternal()
        {
            if (!m_visibilityBuffer || m_buffersDirty)
            {
                CreateBuffers();
            }

            AttachBufferToSlot(NodesSlotName, m_nodesBuffer);
            AttachBufferToSlot(TriangleVerticesSlotName, m_triangleVerticesBuffer);
            AttachBufferToSlot(SamplesSlotName, m_samplesBuffer);
            AttachBufferToSlot(VisibilitySlotName, m_visibilityBuffer);

            ComputePass::BuildInternal();
        }

        void RayTracedShadowsPass::FrameBeginInternal(FramePrepareParams params)
        {
            const uint32_t sampleCount = static_cast<uint32_t>(m_samples.size());

            if (auto* srg = m_shaderResourceGroup.get())
            {
                srg->SetConstant(m_toLightIndex, m_params.m_toLight.GetNormalizedSafe());
                srg->SetConstant(m_maxDistanceIndex, m_params.m_maxDistance);
                srg->SetConstant(m_normalBiasIndex, m_params.m_normalBias);
                srg->SetConstant(m_sampleCountIndex, sampleCount);
                srg->SetConstant(m_nodeCountIndex, m_bvh.GetNodeCount());
            }

            // One thread per sample.
            SetTargetThreadCounts(AZStd::max<uint32_t>(1, sampleCount), 1, 1);

            ComputePass::FrameBeginInternal(params);
        }
    } // namespace Render
} // namespace AZ
