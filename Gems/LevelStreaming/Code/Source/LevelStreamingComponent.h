/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Math/Aabb.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/containers/unordered_set.h>
#include <AzCore/std/containers/vector.h>

#include "LevelStreamingSettings.h"

namespace LevelStreaming
{
    //! Divides the level's mesh entities into a grid of streamable chunks and streams their rendering
    //! in and out around the active camera. Chunk bounds are the union of the member meshes' world
    //! bounds, so a chunk containing a tall object extends upward and keeps streaming from further away
    //! (the stream test measures distance to the chunk's 3D bounds, not its footprint center).
    //!
    //! Attach one instance to any entity in the level (like the Terrain World components).
    class LevelStreamingComponent
        : public AZ::Component
        , private AZ::TickBus::Handler
    {
    public:
        AZ_COMPONENT(LevelStreamingComponent, "{9B7E5D3C-1A2F-4E6B-8C0D-5F4A3B2C1D0E}");

        LevelStreamingComponent() = default;
        explicit LevelStreamingComponent(const LevelStreamingSettings& settings);

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);

        // AZ::Component
        void Activate() override;
        void Deactivate() override;

    private:
        // AZ::TickBus
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;

        void RebuildChunks();
        void UpdateChunkStreaming(const AZ::Vector3& cameraPosition);
        void SetChunkVisible(size_t chunkIndex, bool visible);
        void RestoreAllHidden();

        struct Chunk
        {
            AZ::Aabb m_bounds = AZ::Aabb::CreateNull();
            AZStd::vector<AZ::EntityId> m_members;
            bool m_active = true;
        };

        LevelStreamingSettings m_settings;
        AZStd::vector<Chunk> m_chunks;
        AZStd::unordered_set<AZ::EntityId> m_hiddenByStreaming;
        float m_timeUntilRebuild = 0.0f;
    };
} // namespace LevelStreaming
