/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "VoxelNavVolumeComponent.h"

#include <AzCore/Component/Entity.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Interface/Interface.h>
#include <AzCore/Math/Color.h>
#include <AzCore/RTTI/BehaviorContext.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/chrono/chrono.h>
#include <AzFramework/Entity/EntityDebugDisplayBus.h>
#include <AzFramework/Physics/Common/PhysicsSceneQueries.h>
#include <AzFramework/Physics/Common/PhysicsSimulatedBody.h>
#include <AzFramework/Physics/PhysicsScene.h>
#include <AzFramework/Physics/PhysicsSystem.h>

#include <cmath>

namespace VoxelNav
{
    namespace
    {
        constexpr AZ::s32 BlockSize = 8;
        constexpr AZ::s32 SubBlockSize = 2;
        constexpr size_t MaxRecentPaths = 8;
        constexpr AZ::u64 MaxDebugVoxels = 30000;

        // Queries use slightly smaller boxes so geometry that merely touches a voxel face (a floor whose top
        // lies exactly on a voxel boundary) does not mark the voxel above it solid.
        constexpr float QueryShrink = 0.96f;

        float ElapsedMs(AZStd::chrono::steady_clock::time_point since)
        {
            return AZStd::chrono::duration<float, AZStd::milli>(AZStd::chrono::steady_clock::now() - since).count();
        }
    } // namespace

    VoxelNavVolumeComponent::VoxelNavVolumeComponent(const VoxelNavSettings& settings)
        : m_settings(settings)
    {
    }

    void VoxelNavVolumeComponent::Reflect(AZ::ReflectContext* context)
    {
        VoxelNavSettings::Reflect(context);

        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<VoxelNavVolumeComponent, AZ::Component>()->Version(1)->Field(
                "Settings", &VoxelNavVolumeComponent::m_settings);
        }

        if (auto* behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behaviorContext->Enum<static_cast<int>(AgentMode::Walk)>("VoxelNavAgentMode_Walk")
                ->Enum<static_cast<int>(AgentMode::Fly)>("VoxelNavAgentMode_Fly");

            behaviorContext->EBus<VoxelNavVolumeRequestBus>("VoxelNavVolumeRequestBus")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Category, "VoxelNav")
                ->Attribute(AZ::Script::Attributes::Module, "voxelnav")
                ->Event("FindPath", &VoxelNavVolumeRequestBus::Events::FindPath)
                ->Event("FindRawPath", &VoxelNavVolumeRequestBus::Events::FindRawPath)
                ->Event("IsNavigable", &VoxelNavVolumeRequestBus::Events::IsNavigable)
                ->Event("GetNearestNavigable", &VoxelNavVolumeRequestBus::Events::GetNearestNavigable)
                ->Event("IsReady", &VoxelNavVolumeRequestBus::Events::IsReady)
                ->Event("Rebuild", &VoxelNavVolumeRequestBus::Events::Rebuild)
                ->Event("GetBounds", &VoxelNavVolumeRequestBus::Events::GetBounds)
                ->Event("ContainsPoint", &VoxelNavVolumeRequestBus::Events::ContainsPoint)
                ->Event("GetNavigableVoxelCount", &VoxelNavVolumeRequestBus::Events::GetNavigableVoxelCount);
        }
    }

    void VoxelNavVolumeComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("VoxelNavVolumeService"));
    }

    void VoxelNavVolumeComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("VoxelNavVolumeService"));
    }

    void VoxelNavVolumeComponent::GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required)
    {
        required.push_back(AZ_CRC_CE("TransformService"));
    }

    void VoxelNavVolumeComponent::Activate()
    {
        m_sceneInterface = AZ::Interface<AzPhysics::SceneInterface>::Get();
        AZ_Warning("VoxelNav", m_sceneInterface != nullptr, "No physics scene interface: the Voxel Nav Volume on '%s' will be empty.",
            GetEntity() ? GetEntity()->GetName().c_str() : "");
        // Bake on the first tick so every collider of the level has been activated and added to the scene.
        m_bakeRequested = true;
        m_ready = false;
        m_baking = false;
        VoxelNavVolumeRequestBus::Handler::BusConnect(GetEntityId());
        AZ::TickBus::Handler::BusConnect();
    }

    void VoxelNavVolumeComponent::Deactivate()
    {
        AZ::TickBus::Handler::BusDisconnect();
        VoxelNavVolumeRequestBus::Handler::BusDisconnect();
        m_finder.SetNavData(nullptr);
        m_nav.Clear();
        m_recentPaths.clear();
        m_ready = false;
        m_baking = false;
        m_sceneInterface = nullptr;
    }

    void VoxelNavVolumeComponent::OnTick([[maybe_unused]] float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        if (m_bakeRequested)
        {
            StartBake();
        }
        if (m_baking)
        {
            BakeStep(m_settings.m_bakeBudgetMs);
        }
        if (m_ready && (m_settings.m_debugDraw != DebugDrawMode::None || m_settings.m_debugDrawPaths))
        {
            DebugDraw();
        }
    }

    AZ::Aabb VoxelNavVolumeComponent::ComputeWorldBounds() const
    {
        AZ::Vector3 center = AZ::Vector3::CreateZero();
        AZ::TransformBus::EventResult(center, GetEntityId(), &AZ::TransformBus::Events::GetWorldTranslation);
        const AZ::Vector3 halfSize = m_settings.m_size.GetMax(AZ::Vector3(0.0f)) * 0.5f;
        return AZ::Aabb::CreateFromMinMax(center - halfSize, center + halfSize);
    }

    void VoxelNavVolumeComponent::StartBake()
    {
        m_bakeRequested = false;
        m_ready = false;
        m_baking = false;
        m_finder.SetNavData(nullptr);
        m_nav.Clear();
        m_recentPaths.clear();

        const AZ::Aabb bounds = ComputeWorldBounds();
        if (!m_grid.Initialize(bounds, m_settings.m_voxelSize))
        {
            AZ_Error("VoxelNav", false,
                "Voxel Nav Volume on '%s': cannot build a %.1f x %.1f x %.1f volume with %.3f m voxels (empty size or more than %llu voxels).",
                GetEntity() ? GetEntity()->GetName().c_str() : "", m_settings.m_size.GetX(), m_settings.m_size.GetY(),
                m_settings.m_size.GetZ(), m_settings.m_voxelSize, static_cast<unsigned long long>(VoxelGrid::MaxVoxelCount));
            return;
        }
        m_grid.ClearSolid();
        m_blocksX = (m_grid.SizeX() + BlockSize - 1) / BlockSize;
        m_blocksY = (m_grid.SizeY() + BlockSize - 1) / BlockSize;
        m_blocksZ = (m_grid.SizeZ() + BlockSize - 1) / BlockSize;
        m_blockCount = static_cast<AZ::u64>(m_blocksX) * static_cast<AZ::u64>(m_blocksY) * static_cast<AZ::u64>(m_blocksZ);
        m_nextBlock = 0;
        m_bakeSeconds = 0.0f;
        m_overlapQueries = 0;
        m_baking = true;
    }

    void VoxelNavVolumeComponent::BakeStep(float budgetMs)
    {
        const auto started = AZStd::chrono::steady_clock::now();
        while (m_nextBlock < m_blockCount)
        {
            BakeBlock(m_nextBlock++);
            if (budgetMs > 0.0f && ElapsedMs(started) >= budgetMs)
            {
                break;
            }
        }
        m_bakeSeconds += ElapsedMs(started) * 0.001f;
        if (m_nextBlock >= m_blockCount)
        {
            FinishBake();
        }
    }

    void VoxelNavVolumeComponent::BakeBlock(AZ::u64 blockIndex)
    {
        const AZ::s32 bx = static_cast<AZ::s32>(blockIndex % static_cast<AZ::u64>(m_blocksX));
        const AZ::s32 by = static_cast<AZ::s32>((blockIndex / static_cast<AZ::u64>(m_blocksX)) % static_cast<AZ::u64>(m_blocksY));
        const AZ::s32 bz = static_cast<AZ::s32>(blockIndex / (static_cast<AZ::u64>(m_blocksX) * static_cast<AZ::u64>(m_blocksY)));

        auto cellBox = [this](const GridCoord& min, AZ::s32 span) -> AZ::Aabb
        {
            const GridCoord max(
                AZStd::min(min.m_x + span, m_grid.SizeX()), AZStd::min(min.m_y + span, m_grid.SizeY()),
                AZStd::min(min.m_z + span, m_grid.SizeZ()));
            return AZ::Aabb::CreateFromMinMax(m_grid.VoxelMin(min), m_grid.VoxelMin(max));
        };

        const GridCoord blockMin(bx * BlockSize, by * BlockSize, bz * BlockSize);
        if (!BoxOverlapsCollision(cellBox(blockMin, BlockSize)))
        {
            return;
        }

        // Hierarchical refinement: 8^3 block -> 2^3 sub-blocks -> voxels, so empty space costs one query.
        for (AZ::s32 sz = blockMin.m_z; sz < blockMin.m_z + BlockSize && sz < m_grid.SizeZ(); sz += SubBlockSize)
        {
            for (AZ::s32 sy = blockMin.m_y; sy < blockMin.m_y + BlockSize && sy < m_grid.SizeY(); sy += SubBlockSize)
            {
                for (AZ::s32 sx = blockMin.m_x; sx < blockMin.m_x + BlockSize && sx < m_grid.SizeX(); sx += SubBlockSize)
                {
                    const GridCoord subMin(sx, sy, sz);
                    if (!BoxOverlapsCollision(cellBox(subMin, SubBlockSize)))
                    {
                        continue;
                    }
                    for (AZ::s32 z = sz; z < sz + SubBlockSize && z < m_grid.SizeZ(); ++z)
                    {
                        for (AZ::s32 y = sy; y < sy + SubBlockSize && y < m_grid.SizeY(); ++y)
                        {
                            for (AZ::s32 x = sx; x < sx + SubBlockSize && x < m_grid.SizeX(); ++x)
                            {
                                const GridCoord voxel(x, y, z);
                                if (BoxOverlapsCollision(m_grid.VoxelAabb(voxel)))
                                {
                                    m_grid.SetSolid(voxel, true);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    bool VoxelNavVolumeComponent::BoxOverlapsCollision(const AZ::Aabb& box)
    {
        if (!m_sceneInterface)
        {
            return false;
        }
        const AzPhysics::SceneHandle sceneHandle = m_sceneInterface->GetSceneHandle(AzPhysics::DefaultPhysicsSceneName);
        if (sceneHandle == AzPhysics::InvalidSceneHandle)
        {
            return false;
        }

        const AZ::EntityId self = GetEntityId();
        AzPhysics::OverlapRequest request = AzPhysics::OverlapRequestHelpers::CreateBoxOverlapRequest(
            box.GetExtents() * QueryShrink, AZ::Transform::CreateTranslation(box.GetCenter()),
            [self](const AzPhysics::SimulatedBody* body, [[maybe_unused]] const Physics::Shape* shape)
            {
                return body == nullptr || body->GetEntityId() != self;
            });
        request.m_queryType =
            m_settings.m_includeDynamicBodies ? AzPhysics::SceneQuery::QueryType::StaticAndDynamic : AzPhysics::SceneQuery::QueryType::Static;
        request.m_collisionGroup = AzPhysics::CollisionGroup::All;
        request.m_maxResults = 1;

        ++m_overlapQueries;
        const AzPhysics::SceneQueryHits hits = m_sceneInterface->QueryScene(sceneHandle, &request);
        return !hits.m_hits.empty();
    }

    void VoxelNavVolumeComponent::FinishBake()
    {
        m_baking = false;
        m_nav.Build(m_grid, m_settings.ToAgentParams());
        m_finder.SetNavData(&m_nav);
        m_ready = true;
        AZ_Printf("VoxelNav", "Voxel Nav Volume '%s': baked %d x %d x %d voxels (%.2f m) in %.2f s with %u overlap queries: %llu solid, %llu navigable (%s agent).\n",
            GetEntity() ? GetEntity()->GetName().c_str() : "", m_grid.SizeX(), m_grid.SizeY(), m_grid.SizeZ(), m_grid.VoxelSize(),
            m_bakeSeconds, m_overlapQueries, static_cast<unsigned long long>(m_grid.SolidCount()),
            static_cast<unsigned long long>(m_nav.NavigableCount()), m_settings.m_agentMode == AgentMode::Walk ? "walk" : "fly");
        VoxelNavNotificationBus::Event(GetEntityId(), &VoxelNavNotificationBus::Events::OnNavVolumeBaked);
    }

    bool VoxelNavVolumeComponent::SnapToNavigable(const AZ::Vector3& position, GridCoord& out, bool& wasExact) const
    {
        const GridCoord cell = m_grid.WorldToVoxel(position);
        if (m_nav.IsNavigable(cell))
        {
            out = cell;
            wasExact = true;
            return true;
        }
        wasExact = false;
        const AZ::s32 radius = static_cast<AZ::s32>(std::ceil(m_settings.m_snapDistance / m_grid.VoxelSize()));
        return m_finder.FindNearestNavigable(cell, radius, out);
    }

    AZ::Vector3 VoxelNavVolumeComponent::ToWorld(const GridCoord& coord) const
    {
        return m_settings.m_agentMode == AgentMode::Walk ? m_grid.VoxelBottomCenter(coord) : m_grid.VoxelCenter(coord);
    }

    AZStd::vector<AZ::Vector3> VoxelNavVolumeComponent::Query(const AZ::Vector3& start, const AZ::Vector3& goal, bool smooth)
    {
        AZStd::vector<AZ::Vector3> result;
        if (!m_ready)
        {
            return result;
        }

        GridCoord startCell;
        GridCoord goalCell;
        bool startExact = false;
        bool goalExact = false;
        if (!SnapToNavigable(start, startCell, startExact) || !SnapToNavigable(goal, goalCell, goalExact))
        {
            return result;
        }

        const PathStatus status = m_finder.FindPath(startCell, goalCell, m_settings.m_maxSearchNodes, m_scratchPath);
        if (status != PathStatus::Success)
        {
            AZ_TracePrintf("VoxelNav", "FindPath (%.1f, %.1f, %.1f) -> (%.1f, %.1f, %.1f): %s after %u nodes\n", start.GetX(), start.GetY(),
                start.GetZ(), goal.GetX(), goal.GetY(), goal.GetZ(), ToString(status), m_finder.GetLastExpandedCount());
            return result;
        }

        const AZStd::vector<GridCoord>* cells = &m_scratchPath;
        if (smooth)
        {
            m_finder.SmoothPath(m_scratchPath, m_scratchSmooth);
            cells = &m_scratchSmooth;
        }

        result.reserve(cells->size());
        for (const GridCoord& cell : *cells)
        {
            result.push_back(ToWorld(cell));
        }
        if (startExact && !result.empty())
        {
            result.front() = start;
        }
        if (goalExact && result.size() > 1)
        {
            result.back() = goal;
        }

        if (m_settings.m_debugDrawPaths)
        {
            m_recentPaths.push_back(result);
            while (m_recentPaths.size() > MaxRecentPaths)
            {
                m_recentPaths.pop_front();
            }
        }
        return result;
    }

    AZStd::vector<AZ::Vector3> VoxelNavVolumeComponent::FindPath(const AZ::Vector3& start, const AZ::Vector3& goal)
    {
        return Query(start, goal, m_settings.m_smoothPaths);
    }

    AZStd::vector<AZ::Vector3> VoxelNavVolumeComponent::FindRawPath(const AZ::Vector3& start, const AZ::Vector3& goal)
    {
        return Query(start, goal, false);
    }

    bool VoxelNavVolumeComponent::IsNavigable(const AZ::Vector3& position)
    {
        return m_ready && m_nav.IsNavigable(m_grid.WorldToVoxel(position));
    }

    AZ::Vector3 VoxelNavVolumeComponent::GetNearestNavigable(const AZ::Vector3& position, float maxDistance)
    {
        if (!m_ready)
        {
            return position;
        }
        const GridCoord cell = m_grid.WorldToVoxel(position);
        if (m_nav.IsNavigable(cell))
        {
            return m_settings.m_agentMode == AgentMode::Walk ? AZ::Vector3(position.GetX(), position.GetY(), m_grid.VoxelMin(cell).GetZ())
                                                            : position;
        }
        const AZ::s32 radius = static_cast<AZ::s32>(std::ceil(AZStd::max(0.0f, maxDistance) / m_grid.VoxelSize()));
        GridCoord nearest;
        if (m_finder.FindNearestNavigable(cell, radius, nearest))
        {
            return ToWorld(nearest);
        }
        return position;
    }

    bool VoxelNavVolumeComponent::IsReady()
    {
        return m_ready;
    }

    void VoxelNavVolumeComponent::Rebuild()
    {
        m_bakeRequested = true;
    }

    AZ::Aabb VoxelNavVolumeComponent::GetBounds()
    {
        return m_grid.IsValid() ? m_grid.Bounds() : ComputeWorldBounds();
    }

    bool VoxelNavVolumeComponent::ContainsPoint(const AZ::Vector3& position)
    {
        return GetBounds().Contains(position);
    }

    AZ::u64 VoxelNavVolumeComponent::GetNavigableVoxelCount()
    {
        return m_ready ? m_nav.NavigableCount() : 0;
    }

    AZ::EntityId VoxelNavVolumeComponent::GetVolumeEntityId()
    {
        return GetEntityId();
    }

    void VoxelNavVolumeComponent::DebugDraw()
    {
        AzFramework::DebugDisplayRequestBus::BusPtr debugDisplayBus;
        AzFramework::DebugDisplayRequestBus::Bind(debugDisplayBus, AzFramework::g_defaultSceneEntityDebugDisplayId);
        AzFramework::DebugDisplayRequests* debugDisplay = AzFramework::DebugDisplayRequestBus::FindFirstHandler(debugDisplayBus);
        if (!debugDisplay)
        {
            return;
        }
        const AZ::u32 previousState = debugDisplay->GetState();

        const bool drawNavigable =
            m_settings.m_debugDraw == DebugDrawMode::Navigable || m_settings.m_debugDraw == DebugDrawMode::NavigableAndSolid;
        const bool drawSolid = m_settings.m_debugDraw == DebugDrawMode::Solid || m_settings.m_debugDraw == DebugDrawMode::NavigableAndSolid;
        if (drawNavigable || drawSolid)
        {
            const float inset = 0.1f * m_grid.VoxelSize();
            AZ::u64 drawn = 0;
            const AZ::u64 count = m_grid.VoxelCount();
            for (AZ::u64 i = 0; i < count && drawn < MaxDebugVoxels; ++i)
            {
                const GridCoord c = m_grid.Coord(i);
                const bool solid = m_grid.SolidBits().Test(i);
                if (solid && drawSolid)
                {
                    debugDisplay->SetColor(AZ::Color(1.0f, 0.25f, 0.2f, 0.6f));
                }
                else if (!solid && drawNavigable && m_nav.IsNavigable(c))
                {
                    debugDisplay->SetColor(AZ::Color(0.2f, 1.0f, 0.3f, 0.6f));
                }
                else
                {
                    continue;
                }
                const AZ::Aabb box = m_grid.VoxelAabb(c);
                debugDisplay->DrawWireBox(box.GetMin() + AZ::Vector3(inset), box.GetMax() - AZ::Vector3(inset));
                ++drawn;
            }
        }

        if (m_settings.m_debugDrawPaths)
        {
            debugDisplay->SetColor(AZ::Color(1.0f, 0.9f, 0.1f, 1.0f));
            for (const AZStd::vector<AZ::Vector3>& path : m_recentPaths)
            {
                for (size_t i = 0; i < path.size(); ++i)
                {
                    debugDisplay->DrawBall(path[i], 0.15f * m_grid.VoxelSize() + 0.05f, false);
                    if (i > 0)
                    {
                        debugDisplay->DrawLine(path[i - 1], path[i]);
                    }
                }
            }
        }
        debugDisplay->SetState(previousState);
    }
} // namespace VoxelNav
