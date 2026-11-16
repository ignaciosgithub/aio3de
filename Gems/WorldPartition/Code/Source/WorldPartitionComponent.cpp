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
#include <AzCore/Console/Console.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzFramework/Components/CameraBus.h>
#include <WorldPartition/WorldPartitionStreamingSourceBus.h>

namespace WorldPartition
{
    namespace
    {
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
    void WorldPartitionComponent::OnTick(float, AZ::ScriptTimePoint)
    {
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
            states.push_back({ m_cells[index].m_bounds, m_runtimeCells[index].m_loaded });
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
        spawnableEntities->SpawnAllEntities(runtime.m_ticket);
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
        runtime.m_loaded = false;
    }
}
