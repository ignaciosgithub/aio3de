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

#include <AzCore/Jobs/JobFunction.h>

#include <cmath>
#include <cstring>

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

        bool RayTracedShadowsFullscreenPass::IsRebuildInFlight() const
        {
            return m_pendingBuild && !m_pendingBuild->m_ready.load(AZStd::memory_order_acquire);
        }

        bool RayTracedShadowsFullscreenPass::SetOccluderGeometry(AZStd::vector<AZ::BvhTriangle>&& triangles)
        {
            if (IsRebuildInFlight())
            {
                return false;
            }

            auto pending = AZStd::make_shared<PendingBvhBuild>();
            pending->m_triangles = AZStd::move(triangles);
            m_pendingBuild = pending;

            // The job captures only the shared pending state (not the pass), so it stays safe
            // even if the pass is destroyed before the build finishes.
            auto* job = AZ::CreateJobFunction(
                [pending]()
                {
                    pending->m_bvh.Build(pending->m_triangles);

                    // Pack the ordered triangles as 3 float4 per triangle (xyz used), matching the
                    // shader's m_triangleVertices indexing (3 * slot + 0/1/2).
                    const AZStd::vector<AZ::BvhTriangle>& ordered = pending->m_bvh.GetOrderedTriangles();
                    pending->m_packedVertices.reserve(ordered.size() * 3);
                    for (const AZ::BvhTriangle& tri : ordered)
                    {
                        pending->m_packedVertices.emplace_back(tri.m_v0.GetX(), tri.m_v0.GetY(), tri.m_v0.GetZ(), 0.0f);
                        pending->m_packedVertices.emplace_back(tri.m_v1.GetX(), tri.m_v1.GetY(), tri.m_v1.GetZ(), 0.0f);
                        pending->m_packedVertices.emplace_back(tri.m_v2.GetX(), tri.m_v2.GetY(), tri.m_v2.GetZ(), 0.0f);
                    }

                    pending->m_ready.store(true, AZStd::memory_order_release);
                },
                true); // auto-delete
            job->Start();

            return true;
        }

        void RayTracedShadowsFullscreenPass::SetShadowParams(const AZ::ShadowRayParams& params)
        {
            m_params = params;
        }

        void RayTracedShadowsFullscreenPass::SetLocalLights(
            const AZStd::vector<AZ::LocalShadowLight>& lights, float directionalWeight, float clearance)
        {
            AZStd::vector<LocalShadowLightPacked> packed;
            packed.reserve(AZStd::min<size_t>(lights.size(), MaxLocalLights));
            for (const AZ::LocalShadowLight& light : lights)
            {
                if (packed.size() == MaxLocalLights)
                {
                    break;
                }
                LocalShadowLightPacked& out = packed.emplace_back();
                light.m_position.StoreToFloat3(out.m_position);
                out.m_radius = light.m_radius;
                light.m_diskNormal.StoreToFloat3(out.m_diskNormal);
                out.m_intensity = light.m_intensity;
                light.m_spotDirection.StoreToFloat3(out.m_spotDirection);
                out.m_invAttenuationRadiusSquared = light.m_invAttenuationRadiusSquared;
                out.m_cosInnerConeAngle = light.m_cosInnerConeAngle;
                out.m_cosOuterConeAngle = light.m_cosOuterConeAngle;
                out.m_padding[0] = out.m_padding[1] = 0.0f;
            }
            if (packed.size() != m_localLights.size() ||
                (!packed.empty() && std::memcmp(packed.data(), m_localLights.data(), packed.size() * sizeof(LocalShadowLightPacked)) != 0))
            {
                m_localLights = AZStd::move(packed);
                m_localLightsDirty = true;
            }
            m_directionalWeight = directionalWeight;
            m_lightClearance = clearance;
        }

        void RayTracedShadowsFullscreenPass::UploadLocalLights()
        {
            // Fixed capacity so moving lights only re-uploads data and never re-creates the buffer.
            if (!m_localLightsBuffer)
            {
                RPI::CommonBufferDescriptor desc;
                desc.m_poolType = RPI::CommonBufferPoolType::ReadOnly;
                desc.m_bufferName = "RayTracedShadowsFullscreen.LocalLights";
                desc.m_elementSize = sizeof(LocalShadowLightPacked);
                desc.m_byteCount = static_cast<uint64_t>(desc.m_elementSize) * MaxLocalLights;
                m_localLightsBuffer = RPI::BufferSystemInterface::Get()->CreateBufferFromCommonPool(desc);
                m_localLightsDirty = true;
            }
            if (m_localLightsBuffer && m_localLightsDirty && !m_localLights.empty())
            {
                m_localLightsBuffer->UpdateData(m_localLights.data(), m_localLights.size() * sizeof(LocalShadowLightPacked), 0);
            }
            m_localLightsDirty = false;
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
            // Swap in a finished background BVH build, if any.
            if (m_pendingBuild && m_pendingBuild->m_ready.load(AZStd::memory_order_acquire))
            {
                m_bvh = AZStd::move(m_pendingBuild->m_bvh);
                m_triangleVertices = AZStd::move(m_pendingBuild->m_packedVertices);
                m_pendingBuild.reset();
                m_buffersDirty = true;
            }

            if (!m_nodesBuffer || m_buffersDirty)
            {
                CreateBuffers();
            }

            UploadLocalLights();

            auto* srg = m_shaderResourceGroup.get();
            if (srg && m_nodesBuffer && m_triangleVerticesBuffer && m_localLightsBuffer)
            {
                srg->SetBufferView(m_localLightsBufferIndex, m_localLightsBuffer->GetBufferView());
                srg->SetConstant(m_localLightCountIndex, static_cast<uint32_t>(m_localLights.size()));
                srg->SetConstant(m_directionalWeightIndex, m_directionalWeight);
                srg->SetConstant(m_lightClearanceIndex, m_lightClearance);
                srg->SetBufferView(m_nodesBufferIndex, m_nodesBuffer->GetBufferView());
                srg->SetBufferView(m_triangleVerticesBufferIndex, m_triangleVerticesBuffer->GetBufferView());
                srg->SetConstant(m_toLightIndex, m_params.m_toLight.GetNormalizedSafe());
                srg->SetConstant(m_maxDistanceIndex, m_params.m_maxDistance);
                srg->SetConstant(m_rayBiasIndex, m_params.m_normalBias);
                srg->SetConstant(m_shadowFactorIndex, m_shadowFactor);
                srg->SetConstant(m_nodeCountIndex, m_bvh.GetNodeCount());

                const uint32_t sampleCount = AZStd::max(1u, m_softParams.m_sampleCount);
                const float angularRadius = AZStd::clamp(m_softParams.m_angularRadius, 0.0f, 1.0f);
                srg->SetConstant(m_tanAngularRadiusIndex, std::tan(angularRadius));
                srg->SetConstant(m_sampleCountIndex, sampleCount);
                srg->SetConstant(m_probeCountIndex, AZ::GetSoftShadowProbeCount(sampleCount));
                srg->SetConstant(m_adaptiveIndex, m_softParams.m_adaptive ? 1u : 0u);
                if (m_animateNoise)
                {
                    ++m_noiseFrame;
                }
                srg->SetConstant(m_noiseFrameIndex, m_animateNoise ? m_noiseFrame : 0u);
            }

            FullscreenTrianglePass::FrameBeginInternal(params);
        }
    } // namespace Render
} // namespace AZ
