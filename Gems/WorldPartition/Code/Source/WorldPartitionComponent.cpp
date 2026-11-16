/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "WorldPartitionComponent.h"

#include <AzCore/Asset/AssetManager.h>
#include <AzCore/Asset/AssetSerializer.h>
#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Console/Console.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/std/parallel/lock.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzFramework/Components/CameraBus.h>
#include <AzFramework/Visibility/BoundsBus.h>
#include <WorldPartition/WorldPartitionStreamingSourceBus.h>
#include <AzCore/std/utility/move.h>

namespace WorldPartition
{
    namespace
    {
        constexpr float BoundsRefreshInterval = 1.0f;
        constexpr uint32_t MaxCellsPerBoundsRefresh = 4;

        AZStd::vector<WorldPartitionComponent*> s_activePartitions;
        AZ_CVAR(bool, wp_enabled, true, nullptr, AZ::ConsoleFunctorFlags::DontReplicate,
            "Enable world partition streaming; false requests all cells without a load budget.");
    }

    void WorldPartitionCell::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<WorldPartitionCell>()
                ->Version(1)
                ->Field("X", &WorldPartitionCell::m_x)
                ->Field("Y", &WorldPartitionCell::m_y)
                ->Field("Z", &WorldPartitionCell::m_z)
                ->Field("Bounds", &WorldPartitionCell::m_bounds)
                ->Field("Spawnable", &WorldPartitionCell::m_spawnable)
                ->Field("EntityCount", &WorldPartitionCell::m_entityCount);
        }
    }

    void WorldPartitionComponent::Reflect(AZ::ReflectContext* context)
    {
        WorldPartitionCell::Reflect(context);
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<WorldPartitionComponent, AZ::Component>()
                ->Version(1)
                ->Field("CellSize", &WorldPartitionComponent::m_cellSize)
                ->Field("LoadRadius", &WorldPartitionComponent::m_loadRadius)
                ->Field("UnloadRadius", &WorldPartitionComponent::m_unloadRadius)
                ->Field("MaxCellLoadsPerFrame", &WorldPartitionComponent::m_maxCellLoadsPerFrame)
                ->Field("StreamByDefault", &WorldPartitionComponent::m_streamByDefault)
                ->Field("TwoDimensional", &WorldPartitionComponent::m_twoDimensional)
                ->Field("Cells", &WorldPartitionComponent::m_cells);
        }
    }

    void WorldPartitionComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("WorldPartitionService"));
    }
    void WorldPartitionComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("WorldPartitionService"));
    }
    const AZStd::vector<WorldPartitionComponent*>& WorldPartitionComponent::GetActivePartitions()
    {
        return s_activePartitions;
    }

    void WorldPartitionComponent::Activate()
    {
        m_runtimeCells.clear();
        m_runtimeCells.resize(m_cells.size());
        m_streamBounds.clear();
        m_streamBounds.reserve(m_cells.size());
        for (const WorldPartitionCell& cell : m_cells)
        {
            m_streamBounds.push_back(cell.m_bounds);
        }
        m_boundsRefreshTimer = 0.0f;
        m_nextBoundsRefreshCell = 0;
        {
            AZStd::lock_guard<AZStd::mutex> lock(m_pendingSpawnResults->m_mutex);
            m_pendingSpawnResults->m_results.clear();
        }
        s_activePartitions.push_back(this);
        AZ::TickBus::Handler::BusConnect();
    }
    void WorldPartitionComponent::Deactivate()
    {
        AZ::TickBus::Handler::BusDisconnect();
        s_activePartitions.erase(AZStd::remove(s_activePartitions.begin(), s_activePartitions.end(), this), s_activePartitions.end());
        for (uint32_t index = 0; index < m_runtimeCells.size(); ++index)
        {
            UnloadCell(index);
        }
        m_runtimeCells.clear();
        AZStd::lock_guard<AZStd::mutex> lock(m_pendingSpawnResults->m_mutex);
        m_pendingSpawnResults->m_results.clear();
    }
    uint32_t WorldPartitionComponent::GetLoadedCellCount() const
    {
        return static_cast<uint32_t>(AZStd::count_if(m_runtimeCells.begin(), m_runtimeCells.end(),
            [](const RuntimeCell& cell) { return cell.m_loaded; }));
    }
    uint32_t WorldPartitionComponent::GetSpawnedEntityCount() const
    {
        uint32_t count = 0;
        for (size_t index = 0; index < m_runtimeCells.size() && index < m_cells.size(); ++index)
        {
            if (m_runtimeCells[index].m_loaded)
            {
                count += m_cells[index].m_entityCount;
            }
        }
        return count;
    }
    uint32_t WorldPartitionComponent::GetExpandedCellBoundsCount() const
    {
        uint32_t count = 0;
        for (size_t index = 0; index < m_cells.size() && index < m_streamBounds.size(); ++index)
        {
            count += m_streamBounds[index] != m_cells[index].m_bounds;
        }
        return count;
    }
    void WorldPartitionComponent::ProcessPendingSpawnResults()
    {
        AZStd::vector<PendingSpawnResult> pendingResults;
        {
            AZStd::lock_guard<AZStd::mutex> lock(m_pendingSpawnResults->m_mutex);
            pendingResults.swap(m_pendingSpawnResults->m_results);
        }

        for (PendingSpawnResult& result : pendingResults)
        {
            if (result.m_cellIndex >= m_runtimeCells.size())
            {
                continue;
            }
            RuntimeCell& runtime = m_runtimeCells[result.m_cellIndex];
            if (runtime.m_loaded && runtime.m_ticket.GetId() == result.m_ticketId)
            {
                runtime.m_entities = AZStd::move(result.m_entityIds);
            }
        }
    }
    void WorldPartitionComponent::RefreshLoadedCellBounds()
    {
        if (m_runtimeCells.empty() || m_streamBounds.size() != m_runtimeCells.size())
        {
            return;
        }

        uint32_t refreshedCells = 0;
        size_t examinedCells = 0;
        while (examinedCells < m_runtimeCells.size() && refreshedCells < MaxCellsPerBoundsRefresh)
        {
            const size_t index = m_nextBoundsRefreshCell;
            m_nextBoundsRefreshCell = (m_nextBoundsRefreshCell + 1) % m_runtimeCells.size();
            ++examinedCells;

            RuntimeCell& runtime = m_runtimeCells[index];
            if (!runtime.m_loaded)
            {
                continue;
            }

            AZStd::vector<AZ::Aabb> entityBounds;
            entityBounds.reserve(runtime.m_entities.size());
            for (const AZ::EntityId entityId : runtime.m_entities)
            {
                AZ::Entity* entity = nullptr;
                AZ::ComponentApplicationBus::BroadcastResult(
                    entity, &AZ::ComponentApplicationRequests::FindEntity, entityId);
                if (entity && entity->GetState() == AZ::Entity::State::Active)
                {
                    entityBounds.push_back(AzFramework::CalculateEntityWorldBoundsUnion(entity));
                }
            }
            m_streamBounds[index] = ExpandStreamBounds(m_streamBounds[index], entityBounds);
            ++refreshedCells;
        }
    }
    void WorldPartitionComponent::OnTick(float deltaTime, AZ::ScriptTimePoint)
    {
        ProcessPendingSpawnResults();
        m_boundsRefreshTimer += deltaTime;
        if (m_boundsRefreshTimer >= BoundsRefreshInterval)
        {
            m_boundsRefreshTimer = 0.0f;
            RefreshLoadedCellBounds();
        }

        AZStd::vector<StreamingSource> sources;
        WorldPartitionStreamingSourceRequestBus::Broadcast(&WorldPartitionStreamingSourceRequests::GetStreamingSources, sources);
        if (!WorldPartitionStreamingSourceRequestBus::HasHandlers())
        {
            if (Camera::ActiveCameraRequestBus::HasHandlers())
            {
                AZ::Transform cameraTransform = AZ::Transform::CreateIdentity();
                Camera::ActiveCameraRequestBus::BroadcastResult(
                    cameraTransform, &Camera::ActiveCameraRequests::GetActiveCameraTransform);
                sources.push_back({ cameraTransform.GetTranslation(), 0.0f });
            }
        }

        if (!wp_enabled)
        {
            for (uint32_t index = 0; index < m_cells.size(); ++index)
            {
                if (!m_runtimeCells[index].m_loaded)
                {
                    LoadCell(index);
                }
            }
            return;
        }
        if (sources.empty())
        {
            return;
        }

        AZStd::vector<CellStreamState> states;
        states.reserve(m_cells.size());
        for (size_t index = 0; index < m_cells.size(); ++index)
        {
            states.push_back({ m_streamBounds[index], m_runtimeCells[index].m_loaded });
        }
        StreamingSettings settings{ m_loadRadius, AZStd::max(m_unloadRadius, m_loadRadius),
            m_maxCellLoadsPerFrame, m_twoDimensional };
        StreamingPlan plan = PlanStreaming(states, sources, settings);
        for (uint32_t index : plan.m_unload)
        {
            UnloadCell(index);
        }
        for (uint32_t index : plan.m_load)
        {
            LoadCell(index);
        }
    }

    void WorldPartitionComponent::LoadCell(uint32_t index)
    {
        if (index >= m_cells.size() || index >= m_runtimeCells.size() || m_runtimeCells[index].m_loaded)
        {
            return;
        }
        if (!m_cells[index].m_spawnable.GetId().IsValid())
        {
            return;
        }
        auto* spawnableEntities = AzFramework::SpawnableEntitiesInterface::Get();
        if (!spawnableEntities)
        {
            return;
        }
        RuntimeCell& runtime = m_runtimeCells[index];
        runtime.m_asset = AZ::Data::AssetManager::Instance().GetAsset<AzFramework::Spawnable>(
            m_cells[index].m_spawnable.GetId(), AZ::Data::AssetLoadBehavior::QueueLoad);
        runtime.m_ticket = AzFramework::EntitySpawnTicket(runtime.m_asset);
        if (!runtime.m_ticket.IsValid())
        {
            runtime.m_ticket = {};
            runtime.m_asset.Release();
            return;
        }
        const AZStd::shared_ptr<PendingSpawnResults> pendingSpawnResults = m_pendingSpawnResults;
        AzFramework::SpawnAllEntitiesOptionalArgs optionalArgs;
        optionalArgs.m_completionCallback = [pendingSpawnResults, index](
            AzFramework::EntitySpawnTicket::Id ticketId, AzFramework::SpawnableConstEntityContainerView entities)
        {
            PendingSpawnResult result;
            result.m_cellIndex = index;
            result.m_ticketId = ticketId;
            result.m_entityIds.reserve(entities.size());
            for (const AZ::Entity* entity : entities)
            {
                if (entity)
                {
                    result.m_entityIds.push_back(entity->GetId());
                }
            }
            AZStd::lock_guard<AZStd::mutex> lock(pendingSpawnResults->m_mutex);
            pendingSpawnResults->m_results.push_back(AZStd::move(result));
        };
        spawnableEntities->SpawnAllEntities(runtime.m_ticket, AZStd::move(optionalArgs));
        runtime.m_loaded = true;
    }

    void WorldPartitionComponent::UnloadCell(uint32_t index)
    {
        if (index >= m_runtimeCells.size())
        {
            return;
        }
        RuntimeCell& runtime = m_runtimeCells[index];
        if (runtime.m_ticket.IsValid())
        {
            if (AzFramework::SpawnableEntitiesInterface::Get())
            {
                AzFramework::SpawnableEntitiesInterface::Get()->DespawnAllEntities(runtime.m_ticket);
            }
            runtime.m_ticket = {};
        }
        runtime.m_asset.Release();
        runtime.m_entities.clear();
        runtime.m_loaded = false;
    }
}
