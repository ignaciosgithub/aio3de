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
#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/parallel/mutex.h>
#include <AzCore/std/smart_ptr/make_shared.h>
#include <AzCore/std/smart_ptr/shared_ptr.h>
#include <AzFramework/Spawnable/Spawnable.h>
#include <AzFramework/Spawnable/SpawnableEntitiesInterface.h>
#include <WorldPartition/WorldPartitionStreamingPlanner.h>
#include <cstdint>

namespace WorldPartition
{
    struct WorldPartitionCell
    {
        AZ_TYPE_INFO(WorldPartitionCell, "{49265599-8310-4CE1-A600-1DD649ED7FC9}");
        AZ_CLASS_ALLOCATOR(WorldPartitionCell, AZ::SystemAllocator);
        static void Reflect(AZ::ReflectContext* context);

        int32_t m_x = 0;
        int32_t m_y = 0;
        int32_t m_z = 0;
        AZ::Aabb m_bounds = AZ::Aabb::CreateNull();
        AZ::Data::Asset<AzFramework::Spawnable> m_spawnable{ AZ::Data::AssetLoadBehavior::NoLoad };
        uint32_t m_entityCount = 0;
    };

    class WorldPartitionComponent : public AZ::Component, private AZ::TickBus::Handler
    {
    public:
        AZ_COMPONENT(WorldPartitionComponent, "{FC97655A-6FE4-4E0A-B75D-F9F87BD6D5B9}");
        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);
        static const AZStd::vector<WorldPartitionComponent*>& GetActivePartitions();

        void Activate() override;
        void Deactivate() override;
        uint32_t GetLoadedCellCount() const;
        uint32_t GetSpawnedEntityCount() const;
        uint32_t GetExpandedCellBoundsCount() const;

        float m_cellSize = 64.0f;
        float m_loadRadius = 128.0f;
        float m_unloadRadius = 160.0f;
        uint32_t m_maxCellLoadsPerFrame = 2;
        bool m_streamByDefault = true;
        bool m_twoDimensional = true;
        AZStd::vector<WorldPartitionCell> m_cells;

    private:
        struct PendingSpawnResult
        {
            uint32_t m_cellIndex = 0;
            AzFramework::EntitySpawnTicket::Id m_ticketId = 0;
            AZStd::vector<AZ::EntityId> m_entityIds;
        };
        struct PendingSpawnResults
        {
            AZStd::mutex m_mutex;
            AZStd::vector<PendingSpawnResult> m_results;
        };
        struct RuntimeCell
        {
            AZ::Data::Asset<AzFramework::Spawnable> m_asset{ AZ::Data::AssetLoadBehavior::NoLoad };
            AzFramework::EntitySpawnTicket m_ticket;
            AZStd::vector<AZ::EntityId> m_entities;
            bool m_loaded = false;
        };
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;
        void ProcessPendingSpawnResults();
        void RefreshLoadedCellBounds();
        void LoadCell(uint32_t index);
        void UnloadCell(uint32_t index);

        AZStd::vector<RuntimeCell> m_runtimeCells;
        AZStd::vector<AZ::Aabb> m_streamBounds;
        AZStd::shared_ptr<PendingSpawnResults> m_pendingSpawnResults = AZStd::make_shared<PendingSpawnResults>();
        float m_boundsRefreshTimer = 0.0f;
        size_t m_nextBoundsRefreshCell = 0;
    };
}
