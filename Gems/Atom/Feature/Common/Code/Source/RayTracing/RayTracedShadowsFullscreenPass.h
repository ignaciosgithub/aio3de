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
#include <AzCore/std/parallel/atomic.h>
#include <AzCore/std/smart_ptr/shared_ptr.h>

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
        //! buffer, casts occlusion rays toward the directional light and up to MaxLocalLights point / spot / disk lights against a software BVH
        //! (AZ::RayTracingBvh built on the CPU over the scene's mesh triangles), and darkens the
        //! pixel multiplicatively by the occluded fraction of its incoming light (each light weighted by
        //! the illuminance it delivers, AZ::CombineShadowVisibility). One ray gives a hard shadow; several
        //! rays spread over the light's disk give a soft shadow (AZ::ComputeSoftShadowVisibility). Uses only a standard pixel shader + structured
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

            //! Kicks off an asynchronous BVH build over the occluder \p triangles (world space)
            //! on a background job; the result is swapped in (and the GPU buffers re-created) at
            //! the start of the first frame after the build finishes, so rebuilds never stall
            //! the frame. Returns false (and does nothing) if a build is already in flight.
            bool SetOccluderGeometry(AZStd::vector<AZ::BvhTriangle>&& triangles);

            //! True while a background BVH build kicked off by SetOccluderGeometry is running.
            bool IsRebuildInFlight() const;

            //! Sets the shadow-ray parameters (direction toward the light, max distance, ray bias).
            void SetShadowParams(const AZ::ShadowRayParams& params);

            //! Sets the multiplier applied to shadowed pixels (0 = black, 1 = invisible).
            void SetShadowFactor(float factor) { m_shadowFactor = factor; }

            //! Sets the soft-shadow settings (light disk angular radius, rays per pixel, adaptive
            //! probes). m_rotation is ignored: the shader rotates the pattern per pixel.
            void SetSoftShadowParams(const AZ::SoftShadowParams& soft) { m_softParams = soft; }

            //! When true the per-pixel pattern rotation changes every frame, so TAA averages more
            //! rays over time; when false the pattern is static (no shimmer without TAA).
            void SetAnimateNoise(bool animate) { m_animateNoise = animate; }

            //! Most local (point / spot / disk) lights the pass shades per frame.
            static constexpr uint32_t MaxLocalLights = 16;

            //! Sets the local lights that cast shadows (the first MaxLocalLights are used) and how
            //! the directional light's shadow is weighted against them: \p directionalWeight is its
            //! illuminance (0 = no directional shadow). Local-light rays stop \p clearance meters
            //! before the light.
            void SetLocalLights(const AZStd::vector<AZ::LocalShadowLight>& lights, float directionalWeight, float clearance);

        protected:
            RayTracedShadowsFullscreenPass(const RPI::PassDescriptor& descriptor);

            // Pass behavior overrides...
            void FrameBeginInternal(FramePrepareParams params) override;

        private:
            void CreateBuffers();
            void UploadLocalLights();

            //! GPU layout of AZ::LocalShadowLight; mirrors the shader's LocalShadowLight.
            struct LocalShadowLightPacked
            {
                float m_position[3];
                float m_radius;
                float m_diskNormal[3];
                float m_intensity;
                float m_spotDirection[3];
                float m_invAttenuationRadiusSquared;
                float m_cosInnerConeAngle;
                float m_cosOuterConeAngle;
                float m_padding[2];
            };
            static_assert(sizeof(LocalShadowLightPacked) == 64, "Must match LocalShadowLight in RayTracedShadowsFullscreen.azsl");

            //! Result of a background BVH build, shared between the pass and the job so the
            //! job stays safe even if the pass is destroyed before it finishes.
            struct PendingBvhBuild
            {
                AZStd::vector<AZ::BvhTriangle> m_triangles;
                AZ::RayTracingBvh m_bvh;
                AZStd::vector<AZ::Vector4> m_packedVertices;
                AZStd::atomic_bool m_ready{ false };
            };

            AZStd::shared_ptr<PendingBvhBuild> m_pendingBuild;

            AZ::RayTracingBvh m_bvh;

            // Packed, GPU-ready geometry derived from m_bvh in SetOccluderGeometry.
            AZStd::vector<AZ::Vector4> m_triangleVertices; // 3 per ordered triangle, xyz used.
            AZ::ShadowRayParams m_params;
            float m_shadowFactor = 0.25f;
            AZ::SoftShadowParams m_softParams;
            bool m_animateNoise = false;
            uint32_t m_noiseFrame = 0;
            AZStd::vector<LocalShadowLightPacked> m_localLights;
            bool m_localLightsDirty = true;
            float m_directionalWeight = 1.0f;
            float m_lightClearance = 0.1f;

            Data::Instance<RPI::Buffer> m_nodesBuffer;
            Data::Instance<RPI::Buffer> m_triangleVerticesBuffer;
            Data::Instance<RPI::Buffer> m_localLightsBuffer;

            bool m_buffersDirty = false;

            // SRG constant binding indices.
            AZ::RHI::ShaderInputNameIndex m_toLightIndex = "m_toLight";
            AZ::RHI::ShaderInputNameIndex m_maxDistanceIndex = "m_maxDistance";
            AZ::RHI::ShaderInputNameIndex m_rayBiasIndex = "m_rayBias";
            AZ::RHI::ShaderInputNameIndex m_shadowFactorIndex = "m_shadowFactor";
            AZ::RHI::ShaderInputNameIndex m_nodeCountIndex = "m_nodeCount";
            AZ::RHI::ShaderInputNameIndex m_tanAngularRadiusIndex = "m_tanAngularRadius";
            AZ::RHI::ShaderInputNameIndex m_sampleCountIndex = "m_sampleCount";
            AZ::RHI::ShaderInputNameIndex m_probeCountIndex = "m_probeCount";
            AZ::RHI::ShaderInputNameIndex m_adaptiveIndex = "m_adaptive";
            AZ::RHI::ShaderInputNameIndex m_noiseFrameIndex = "m_noiseFrame";
            AZ::RHI::ShaderInputNameIndex m_localLightCountIndex = "m_localLightCount";
            AZ::RHI::ShaderInputNameIndex m_directionalWeightIndex = "m_directionalWeight";
            AZ::RHI::ShaderInputNameIndex m_lightClearanceIndex = "m_lightClearance";

            // SRG buffer binding indices (bound directly, not as pass attachments, since
            // read-only common-pool buffers have no attachment id).
            AZ::RHI::ShaderInputNameIndex m_nodesBufferIndex = "m_nodes";
            AZ::RHI::ShaderInputNameIndex m_triangleVerticesBufferIndex = "m_triangleVertices";
            AZ::RHI::ShaderInputNameIndex m_localLightsBufferIndex = "m_localLights";
        };
    } // namespace Render
} // namespace AZ
