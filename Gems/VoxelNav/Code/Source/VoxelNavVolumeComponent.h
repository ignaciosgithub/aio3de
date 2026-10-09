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
#include <AzCore/std/containers/deque.h>
#include <VoxelNav/VoxelNavBus.h>

#include "VoxelGrid.h"
#include "VoxelNavData.h"
#include "VoxelNavSettings.h"
#include "VoxelPathfinder.h"

namespace AzPhysics
{
    class SceneInterface;
}

namespace VoxelNav
{
    //! Bakes the physics colliders inside its volume into a voxel grid and answers 3D path queries.
    class VoxelNavVolumeComponent
        : public AZ::Component
        , private AZ::TickBus::Handler
        , private VoxelNavVolumeRequestBus::Handler
    {
    public:
        AZ_COMPONENT(VoxelNavVolumeComponent, "{7E3C9A1B-2D4F-4B6E-8A0C-1F2E3D4C5B6A}");

        VoxelNavVolumeComponent() = default;
        explicit VoxelNavVolumeComponent(const VoxelNavSettings& settings);

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required);

        // AZ::Component
        void Activate() override;
        void Deactivate() override;

        // VoxelNavVolumeRequestBus
        AZStd::vector<AZ::Vector3> FindPath(const AZ::Vector3& start, const AZ::Vector3& goal) override;
        AZStd::vector<AZ::Vector3> FindRawPath(const AZ::Vector3& start, const AZ::Vector3& goal) override;
        bool IsNavigable(const AZ::Vector3& position) override;
        AZ::Vector3 GetNearestNavigable(const AZ::Vector3& position, float maxDistance) override;
        bool IsReady() override;
        void Rebuild() override;
        AZ::Aabb GetBounds() override;
        bool ContainsPoint(const AZ::Vector3& position) override;
        AZ::u64 GetNavigableVoxelCount() override;
        AZ::EntityId GetVolumeEntityId() override;

    private:
        // AZ::TickBus
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;

        AZ::Aabb ComputeWorldBounds() const;
        void StartBake();
        void BakeStep(float budgetMs);
        void BakeBlock(AZ::u64 blockIndex);
        void FinishBake();
        bool BoxOverlapsCollision(const AZ::Aabb& box);

        AZStd::vector<AZ::Vector3> Query(const AZ::Vector3& start, const AZ::Vector3& goal, bool smooth);
        bool SnapToNavigable(const AZ::Vector3& position, GridCoord& out, bool& wasExact) const;
        AZ::Vector3 ToWorld(const GridCoord& coord) const;
        void DebugDraw();

        VoxelNavSettings m_settings;
        VoxelGrid m_grid;
        NavData m_nav;
        VoxelPathfinder m_finder;

        AzPhysics::SceneInterface* m_sceneInterface = nullptr;
        bool m_bakeRequested = true;
        bool m_baking = false;
        bool m_ready = false;
        AZ::u64 m_nextBlock = 0;
        AZ::u64 m_blockCount = 0;
        AZ::s32 m_blocksX = 0;
        AZ::s32 m_blocksY = 0;
        AZ::s32 m_blocksZ = 0;
        float m_bakeSeconds = 0.0f;
        AZ::u32 m_overlapQueries = 0;

        AZStd::vector<GridCoord> m_scratchPath;
        AZStd::vector<GridCoord> m_scratchSmooth;
        AZStd::deque<AZStd::vector<AZ::Vector3>> m_recentPaths;
    };
} // namespace VoxelNav
