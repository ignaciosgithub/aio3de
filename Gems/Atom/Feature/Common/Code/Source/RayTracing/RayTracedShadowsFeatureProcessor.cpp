/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <RayTracing/RayTracedShadowsFeatureProcessor.h>
#include <RayTracing/RayTracedShadowsFullscreenPass.h>

#include <CoreLights/DirectionalLightFeatureProcessor.h>
#include <Mesh/MeshFeatureProcessor.h>

#include <Atom/RPI.Public/Pass/PassFilter.h>
#include <Atom/RPI.Public/Pass/PassSystemInterface.h>
#include <Atom/RPI.Public/Scene.h>
#include <Atom/RPI.Public/View.h>

#include <AzCore/Console/IConsole.h>
#include <AzCore/Math/RayTracedShadows.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace AZ::Render
{
    AZ_CVAR(
        bool,
        r_rayTracedShadows,
        false,
        nullptr,
        AZ::ConsoleFunctorFlags::Null,
        "Enable the hardware-agnostic fullscreen ray-traced shadows pass (software BVH, works on any GPU). "
        "Toggle live from the console to A/B against the standard shadow maps.");

    AZ_CVAR(
        bool,
        r_rayTracedShadowsRebuild,
        false,
        nullptr,
        AZ::ConsoleFunctorFlags::Null,
        "Set to true to rebuild the ray-traced shadows occluder BVH from the current scene geometry "
        "(auto-resets to false). Use after moving/adding meshes.");

    AZ_CVAR(
        float,
        r_rayTracedShadowsFactor,
        0.25f,
        nullptr,
        AZ::ConsoleFunctorFlags::Null,
        "Multiplier applied to shadowed pixels by the ray-traced shadows pass (0 = black, 1 = invisible).");

    AZ_CVAR(
        float,
        r_rayTracedShadowsMaxDistance,
        10000.0f,
        nullptr,
        AZ::ConsoleFunctorFlags::Null,
        "Maximum occlusion-ray distance for the ray-traced shadows pass, in meters.");

    AZ_CVAR(
        float,
        r_rayTracedShadowsBias,
        0.02f,
        nullptr,
        AZ::ConsoleFunctorFlags::Null,
        "Ray-origin offset along the shadow ray to avoid self-shadow acne, in meters.");

    AZ_CVAR(
        uint32_t,
        r_rayTracedShadowsMaxTriangles,
        1000000,
        nullptr,
        AZ::ConsoleFunctorFlags::Null,
        "Maximum number of scene triangles gathered into the ray-traced shadows occluder BVH.");

    AZ_CVAR(
        bool,
        r_rayTracedShadowsPrewarm,
        true,
        nullptr,
        AZ::ConsoleFunctorFlags::Null,
        "Build the ray-traced shadows occluder BVH in the background at level load, while the pass is "
        "still disabled, so the first r_rayTracedShadows enable is instant (small extra load-time cost).");

    AZ_CVAR(
        bool,
        r_rayTracedShadowsAutoRebuild,
        true,
        nullptr,
        AZ::ConsoleFunctorFlags::Null,
        "Automatically rebuild the ray-traced shadows occluder BVH (async, no frame hitch) when meshes "
        "are added, removed or moved in the scene, instead of requiring r_rayTracedShadowsRebuild.");

    AZ_CVAR(
        uint32_t,
        r_rayTracedShadowsAutoRebuildPollFrames,
        30,
        nullptr,
        AZ::ConsoleFunctorFlags::Null,
        "How often (in frames) the auto-rebuild checks nearby meshes (within "
        "r_rayTracedShadowsNearDistance of the camera) for changes.");

    AZ_CVAR(
        uint32_t,
        r_rayTracedShadowsFarPollFrames,
        300,
        nullptr,
        AZ::ConsoleFunctorFlags::Null,
        "How often (in frames) the auto-rebuild checks distant meshes (beyond "
        "r_rayTracedShadowsNearDistance of the camera) for changes. Distant occluder changes are "
        "much less visible, so they are picked up far less often than nearby ones.");

    AZ_CVAR(
        float,
        r_rayTracedShadowsNearDistance,
        50.0f,
        nullptr,
        AZ::ConsoleFunctorFlags::Null,
        "Camera distance (meters) separating 'near' occluder geometry (polled every "
        "r_rayTracedShadowsAutoRebuildPollFrames frames) from 'far' geometry (polled every "
        "r_rayTracedShadowsFarPollFrames frames).");

    AZ_CVAR(
        uint32_t,
        r_rayTracedShadowsMinRebuildFrames,
        60,
        nullptr,
        AZ::ConsoleFunctorFlags::Null,
        "Minimum number of frames between occluder BVH rebuilds. Rate-limits the main-thread "
        "triangle gather when geometry changes constantly (e.g. many moving objects).");

    AZ_CVAR(
        bool,
        r_rayTracedShadowsIncludeDynamicMeshes,
        false,
        nullptr,
        AZ::ConsoleFunctorFlags::Null,
        "Include always-dynamic meshes (particles, skinned characters, constantly-moving props) in "
        "the ray-traced shadows occluder BVH. Off by default: they change every frame and would "
        "force continuous BVH rebuilds, causing large FPS drops.");

    void RayTracedShadowsFeatureProcessor::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<RayTracedShadowsFeatureProcessor, AZ::RPI::FeatureProcessor>()->Version(0);
        }
    }

    void RayTracedShadowsFeatureProcessor::Activate()
    {
        m_passEnabled = false;
        m_geometryUploaded = false;
        m_lastNearGeometryHash = 0;
        m_lastFarGeometryHash = 0;
        m_framesUntilNearPoll = 0;
        m_framesUntilFarPoll = 0;
        m_framesSinceLastRebuild = 1000000; // The first build is never rate-limited.
    }

    void RayTracedShadowsFeatureProcessor::Deactivate()
    {
    }

    RayTracedShadowsFullscreenPass* RayTracedShadowsFeatureProcessor::FindPass() const
    {
        RayTracedShadowsFullscreenPass* result = nullptr;
        auto passFilter =
            AZ::RPI::PassFilter::CreateWithTemplateName(Name("RayTracedShadowsFullscreenTemplate"), GetParentScene());
        AZ::RPI::PassSystemInterface::Get()->ForEachPass(
            passFilter,
            [&result](AZ::RPI::Pass* pass) -> AZ::RPI::PassFilterExecutionFlow
            {
                result = azrtti_cast<RayTracedShadowsFullscreenPass*>(pass);
                return result ? AZ::RPI::PassFilterExecutionFlow::StopVisitingPasses
                              : AZ::RPI::PassFilterExecutionFlow::ContinueVisitingPasses;
            });
        return result;
    }

    void RayTracedShadowsFeatureProcessor::UpdateOccluderGeometry(RayTracedShadowsFullscreenPass* pass)
    {
        if (pass->IsRebuildInFlight())
        {
            return; // Retry next Simulate once the background build finishes.
        }

        AZStd::vector<AZ::BvhTriangle> triangles;
        if (auto* meshFeatureProcessor = GetParentScene()->GetFeatureProcessor<MeshFeatureProcessor>())
        {
            meshFeatureProcessor->GetWorldTriangles(
                triangles, r_rayTracedShadowsMaxTriangles, r_rayTracedShadowsIncludeDynamicMeshes);
        }
        if (pass->SetOccluderGeometry(AZStd::move(triangles)))
        {
            m_geometryUploaded = true;
            m_framesSinceLastRebuild = 0;
        }
    }

    void RayTracedShadowsFeatureProcessor::UpdateShadowParams(RayTracedShadowsFullscreenPass* pass)
    {
        // Default sun direction (high noon-ish) used when the scene has no directional light.
        Vector3 toLight(0.2f, 0.3f, 0.9f);
        if (auto* lightFeatureProcessor = GetParentScene()->GetFeatureProcessor<DirectionalLightFeatureProcessor>())
        {
            // m_direction points from the light toward the scene; the shadow ray goes the other way.
            const Vector3 lightDirection = lightFeatureProcessor->GetFirstLightDirection();
            if (!lightDirection.IsZero())
            {
                toLight = -lightDirection;
            }
        }

        AZ::ShadowRayParams params;
        params.m_toLight = toLight;
        params.m_maxDistance = r_rayTracedShadowsMaxDistance;
        params.m_normalBias = r_rayTracedShadowsBias;
        pass->SetShadowParams(params);
        pass->SetShadowFactor(r_rayTracedShadowsFactor);
    }

    void RayTracedShadowsFeatureProcessor::Simulate([[maybe_unused]] const SimulatePacket& packet)
    {
        RayTracedShadowsFullscreenPass* pass = FindPass();
        if (!pass)
        {
            return;
        }

        const bool enabled = r_rayTracedShadows;
        if (enabled != m_passEnabled)
        {
            pass->SetEnabled(enabled);
            m_passEnabled = enabled;
        }

        if (r_rayTracedShadowsRebuild)
        {
            r_rayTracedShadowsRebuild = false;
            m_geometryUploaded = false;
        }

        // Detect meshes being added/removed/moved (or streamed in at level load) via hashes of
        // the ready models and their transforms, split by camera distance: near geometry is
        // polled often (its shadows are the visible ones), far geometry rarely. A change
        // invalidates the geometry so it gets rebuilt asynchronously below, rate-limited by
        // r_rayTracedShadowsMinRebuildFrames.
        if (enabled || r_rayTracedShadowsPrewarm)
        {
            const bool pollNear = m_framesUntilNearPoll == 0;
            const bool pollFar = m_framesUntilFarPoll == 0;
            if (pollNear || pollFar)
            {
                if (pollNear)
                {
                    m_framesUntilNearPoll = AZStd::max<uint32_t>(1, r_rayTracedShadowsAutoRebuildPollFrames);
                }
                if (pollFar)
                {
                    m_framesUntilFarPoll = AZStd::max<uint32_t>(1, r_rayTracedShadowsFarPollFrames);
                }
                if (auto* meshFeatureProcessor = GetParentScene()->GetFeatureProcessor<MeshFeatureProcessor>())
                {
                    size_t nearHash = 0;
                    size_t farHash = 0;
                    meshFeatureProcessor->GetSceneGeometryHashes(
                        m_cameraPosition,
                        r_rayTracedShadowsNearDistance,
                        r_rayTracedShadowsIncludeDynamicMeshes,
                        nearHash,
                        farHash);

                    bool changed = false;
                    if (pollNear && nearHash != m_lastNearGeometryHash)
                    {
                        m_lastNearGeometryHash = nearHash;
                        changed = true;
                    }
                    if (pollFar && farHash != m_lastFarGeometryHash)
                    {
                        m_lastFarGeometryHash = farHash;
                        changed = true;
                    }
                    if (changed && (r_rayTracedShadowsAutoRebuild || !m_geometryUploaded))
                    {
                        m_geometryUploaded = false;
                    }
                }
            }
            --m_framesUntilNearPoll;
            --m_framesUntilFarPoll;
            ++m_framesSinceLastRebuild;

            // Gathers + kicks the async BVH build; also runs while the pass is disabled when
            // prewarm is on, so the first enable doesn't have to build anything.
            if (!m_geometryUploaded && m_framesSinceLastRebuild >= r_rayTracedShadowsMinRebuildFrames)
            {
                UpdateOccluderGeometry(pass);
            }
        }

        if (!enabled)
        {
            return;
        }

        UpdateShadowParams(pass);
    }

    void RayTracedShadowsFeatureProcessor::Render(const RenderPacket& packet)
    {
        // Track the camera position for the near/far occluder-change polling in Simulate.
        for (const RPI::ViewPtr& view : packet.m_views)
        {
            if (view)
            {
                m_cameraPosition = view->GetViewToWorldMatrix().GetTranslation();
                break;
            }
        }
    }
} // namespace AZ::Render
