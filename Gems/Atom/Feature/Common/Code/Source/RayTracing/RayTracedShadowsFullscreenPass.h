/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzCore/Math/RayTracedShadows.h>
#include <AzCore/Math/RayTracingBvh.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/Math/Vector4.h>
#include <AzCore/Memory/SystemAllocator.h>
#include <AzCore/std/containers/vector.h>

#include <Atom/RHI.Reflect/ShaderInputNameIndex.h>

#include <Atom/RPI.Public/Buffer/Buffer.h>
#include <Atom/RPI.Public/Pass/FullscreenTrianglePass.h>

namespace AZ
{
    namespace Render
    {
        //! Fullscreen hardware-agnostic hard ray-traced shadows.
        //!
        //! For every pixel of the lighting target, reconstructs the world position from the depth
        //! buffer, casts a single occlusion ray toward the directional light against a software BVH
        //! (AZ::RayTracingBvh built on the CPU over the scene's mesh triangles), and darkens the
        //! pixel multiplicatively if occluded. Uses only a standard pixel shader + structured
        //! buffers, with NO dependency on DXR / VK_KHR_ray_tracing or RT cores, so it runs on every
        //! RHI backend and any GPU.
        //!
        //! Wired into MainPipeline but disabled by default; RayTracedShadowsFeatureProcessor
        //! enables it at runtime via the r_rayTracedShadows cvar and feeds it the scene geometry.
        class RayTracedShadowsFullscreenPass final
            : public RPI::FullscreenTrianglePass
        {
            AZ_RPI_PASS(RayTracedShadowsFullscreenPass);

        public:
            AZ_RTTI(RayTracedShadowsFullscreenPass, "{3E7A1B52-9D04-46C8-8E2F-6B5A4C3D2E11}", RPI::FullscreenTrianglePass);
            AZ_CLASS_ALLOCATOR(RayTracedShadowsFullscreenPass, SystemAllocator);
            ~RayTracedShadowsFullscreenPass() = default;

            static RPI::Ptr<RayTracedShadowsFullscreenPass> Create(const RPI::PassDescriptor& descriptor);

            //! Builds the BVH over the occluder \p triangles (world space) and marks the GPU
            //! geometry buffers dirty.
            void SetOccluderGeometry(const AZStd::vector<AZ::BvhTriangle>& triangles);

            //! Sets the shadow-ray parameters (direction toward the light, max distance, ray bias).
            void SetShadowParams(const AZ::ShadowRayParams& params);

            //! Sets the multiplier applied to shadowed pixels (0 = black, 1 = invisible).
            void SetShadowFactor(float factor) { m_shadowFactor = factor; }

        protected:
            RayTracedShadowsFullscreenPass(const RPI::PassDescriptor& descriptor);

            // Pass behavior overrides...
            void FrameBeginInternal(FramePrepareParams params) override;

        private:
            void CreateBuffers();

            AZ::RayTracingBvh m_bvh;

            // Packed, GPU-ready geometry derived from m_bvh in SetOccluderGeometry.
            AZStd::vector<AZ::Vector4> m_triangleVertices; // 3 per ordered triangle, xyz used.
            AZ::ShadowRayParams m_params;
            float m_shadowFactor = 0.25f;

            Data::Instance<RPI::Buffer> m_nodesBuffer;
            Data::Instance<RPI::Buffer> m_triangleVerticesBuffer;

            bool m_buffersDirty = false;

            // SRG constant binding indices.
            AZ::RHI::ShaderInputNameIndex m_toLightIndex = "m_toLight";
            AZ::RHI::ShaderInputNameIndex m_maxDistanceIndex = "m_maxDistance";
            AZ::RHI::ShaderInputNameIndex m_rayBiasIndex = "m_rayBias";
            AZ::RHI::ShaderInputNameIndex m_shadowFactorIndex = "m_shadowFactor";
            AZ::RHI::ShaderInputNameIndex m_nodeCountIndex = "m_nodeCount";

            // SRG buffer binding indices (bound directly, not as pass attachments, since
            // read-only common-pool buffers have no attachment id).
            AZ::RHI::ShaderInputNameIndex m_nodesBufferIndex = "m_nodes";
            AZ::RHI::ShaderInputNameIndex m_triangleVerticesBufferIndex = "m_triangleVertices";
        };
    } // namespace Render
} // namespace AZ
