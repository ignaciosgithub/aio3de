/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <Atom/RPI.Public/FeatureProcessor.h>
#include <Atom/RPI.Public/Buffer/Buffer.h>
#include <AzCore/Math/RayTracedShadows.h>
#include <AzCore/Math/RayTracingBvh.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/Math/Vector4.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/parallel/atomic.h>
#include <AzCore/std/smart_ptr/shared_ptr.h>

namespace AZ::Render
{
    class RayTracedShadowsFullscreenPass;

    //! Drives the fullscreen hardware-agnostic ray-traced shadows pass
    //! (RayTracedShadowsFullscreenPass) from the r_rayTracedShadows* cvars:
    //! - Enables/disables the pass at runtime so it can be A/B toggled live from the console.
    //! - Gathers the scene's mesh triangles (world space) from the MeshFeatureProcessor and feeds
    //!   them to the pass as occluder geometry (rebuilt on enable and via r_rayTracedShadowsRebuild).
    //! - Prewarms the occluder BVH at level load while the pass is still disabled
    //!   (r_rayTracedShadowsPrewarm) so the first enable is instant, and automatically rebuilds it
    //!   when meshes are added/removed (r_rayTracedShadowsAutoRebuild), both via the pass's async
    //!   background build.
    //! - Feeds the first directional light's direction and disk size (Angular diameter) plus the
    //!   max-distance/bias/factor and soft-shadow (samples/softness/adaptive) cvars to the pass
    //!   every frame.
    class RayTracedShadowsFeatureProcessor final
        : public AZ::RPI::FeatureProcessor
    {
    public:
        AZ_CLASS_ALLOCATOR(RayTracedShadowsFeatureProcessor, AZ::SystemAllocator)
        AZ_RTTI(AZ::Render::RayTracedShadowsFeatureProcessor, "{9B41C2D3-64E5-4F86-A7B8-C9D0E1F2A301}", AZ::RPI::FeatureProcessor);

        static void Reflect(AZ::ReflectContext* context);

        RayTracedShadowsFeatureProcessor() = default;
        ~RayTracedShadowsFeatureProcessor() override = default;

        //! FeatureProcessor
        void Activate() override;
        void Deactivate() override;
        void Simulate(const SimulatePacket& packet) override;
        void Render(const RenderPacket& packet) override;

        void PrepareSharedBvhBuffers();
        const Data::Instance<RPI::Buffer>& GetSharedBvhNodesBuffer() const;
        const Data::Instance<RPI::Buffer>& GetSharedBvhTriangleVerticesBuffer() const;
        const Data::Instance<RPI::Buffer>& GetSharedBvhPrimitiveIndicesBuffer() const;
        const Data::Instance<RPI::Buffer>& GetSharedBvhAlbedoBuffer() const;
        uint32_t GetSharedBvhNodeCount() const;
        uint32_t GetSharedBvhTriangleCount() const;
        AZ::Vector3 GetSunDirection() const { return m_sunDirection; }
        AZ::Vector3 GetSunIlluminanceOverPi() const { return m_sunIlluminanceOverPi; }
        AZ::Vector3 GetCameraPosition() const { return m_cameraPosition; }

    private:
        struct SharedBvhBuild
        {
            AZStd::vector<AZ::BvhTriangle> m_triangles;
            AZStd::vector<uint32_t> m_albedo;
            AZ::RayTracingBvh m_bvh;
            AZStd::vector<AZ::Vector4> m_packedVertices;
            AZStd::atomic_bool m_ready{ false };
        };

        struct SharedBvh
        {
            bool BeginBuild(AZStd::vector<AZ::BvhTriangle>&& triangles, AZStd::vector<uint32_t>&& albedo);
            void PrepareBuffers();
            void CreateBuffers();

            AZStd::shared_ptr<SharedBvhBuild> m_pendingBuild;
            AZ::RayTracingBvh m_bvh;
            AZStd::vector<AZ::Vector4> m_packedVertices;
            AZStd::vector<uint32_t> m_albedo;
            Data::Instance<RPI::Buffer> m_nodesBuffer;
            Data::Instance<RPI::Buffer> m_triangleVerticesBuffer;
            Data::Instance<RPI::Buffer> m_primitiveIndicesBuffer;
            Data::Instance<RPI::Buffer> m_albedoBuffer;
            bool m_buffersDirty = true;
        };

        RayTracedShadowsFullscreenPass* FindPass() const;
        void UpdateOccluderGeometry();
        void UpdateShadowParams(RayTracedShadowsFullscreenPass* pass);
        //! Collects every point, simple point, disk and simple spot light in the scene.
        void GatherLocalLights(AZStd::vector<AZ::LocalShadowLight>& lights) const;

        bool m_passEnabled = false;
        bool m_geometryUploaded = false;
        size_t m_lastNearGeometryHash = 0;
        size_t m_lastFarGeometryHash = 0;
        uint32_t m_framesUntilNearPoll = 0;
        uint32_t m_framesUntilFarPoll = 0;
        uint32_t m_framesSinceLastRebuild = 0;
        AZ::Vector3 m_cameraPosition = AZ::Vector3::CreateZero();
        AZ::Vector3 m_sunDirection = AZ::Vector3::CreateZero();
        AZ::Vector3 m_sunIlluminanceOverPi = AZ::Vector3::CreateZero();
        SharedBvh m_sharedBvh;

        // Scratch list of the local lights that cast shadows this frame.
        AZStd::vector<AZ::LocalShadowLight> m_localLights;
    };
} // namespace AZ::Render
