/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <Atom/RPI.Public/FeatureProcessor.h>

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
    //! - Feeds the first directional light's direction plus the max-distance/bias/factor cvars to
    //!   the pass every frame.
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

    private:
        RayTracedShadowsFullscreenPass* FindPass() const;
        void UpdateOccluderGeometry(RayTracedShadowsFullscreenPass* pass);
        void UpdateShadowParams(RayTracedShadowsFullscreenPass* pass);

        bool m_passEnabled = false;
        bool m_geometryUploaded = false;
        uint32_t m_lastReadyModelCount = 0;
        uint32_t m_framesUntilModelCountPoll = 0;
    };
} // namespace AZ::Render
