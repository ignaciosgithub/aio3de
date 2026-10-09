/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Component/ComponentBus.h>
#include <AzCore/EBus/EBus.h>
#include <AzCore/Math/Aabb.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/RTTI/TypeInfoSimple.h>
#include <AzCore/std/containers/vector.h>

namespace VoxelNav
{
    //! How the agent moves through the voxel grid.
    enum class AgentMode : AZ::u8
    {
        //! Grounded agent: needs floor support, limited step up / drop down, body cylinder clearance.
        Walk = 0,
        //! Free-flying agent: sphere clearance, moves in all 26 directions.
        Fly = 1,
    };

    //! Shared query surface of a single Voxel Nav Volume and of the global dispatcher.
    class VoxelNavQueries
    {
    public:
        virtual ~VoxelNavQueries() = default;

        //! Finds a smoothed world-space path from start to goal. Empty when no path exists, the volume is
        //! not baked yet or the points are outside every volume. Walk-mode points are feet positions.
        virtual AZStd::vector<AZ::Vector3> FindPath(const AZ::Vector3& start, const AZ::Vector3& goal) = 0;

        //! Same as FindPath but returns every voxel the agent passes through (no string pulling).
        virtual AZStd::vector<AZ::Vector3> FindRawPath(const AZ::Vector3& start, const AZ::Vector3& goal) = 0;

        //! True when the voxel containing the position can be occupied by the agent.
        virtual bool IsNavigable(const AZ::Vector3& position) = 0;

        //! Nearest navigable position within maxDistance; returns the input unchanged when none exists.
        virtual AZ::Vector3 GetNearestNavigable(const AZ::Vector3& position, float maxDistance) = 0;

        //! True once the voxel grid has been baked from the physics scene.
        virtual bool IsReady() = 0;

        //! Re-bakes the voxel grid from the current physics scene (use after moving level geometry).
        virtual void Rebuild() = 0;
    };

    //! Per-volume requests, addressed by the entity holding the Voxel Nav Volume component.
    class VoxelNavVolumeRequests
        : public VoxelNavQueries
        , public AZ::ComponentBus
    {
    public:
        //! World-space bounds covered by this volume.
        virtual AZ::Aabb GetBounds() = 0;

        //! True when the position lies inside the volume bounds.
        virtual bool ContainsPoint(const AZ::Vector3& position) = 0;

        //! Number of voxels the agent can occupy (0 before the bake completes).
        virtual AZ::u64 GetNavigableVoxelCount() = 0;

        //! Entity holding this volume (lets EnumerateHandlers callers identify the volume).
        virtual AZ::EntityId GetVolumeEntityId() = 0;
    };
    using VoxelNavVolumeRequestBus = AZ::EBus<VoxelNavVolumeRequests>;

    //! Global requests: routed to the volume containing the start position (first active volume otherwise).
    class VoxelNavRequests
        : public VoxelNavQueries
        , public AZ::EBusTraits
    {
    public:
        static const AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Single;
        static const AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::Single;

        //! Number of active Voxel Nav Volumes in the level.
        virtual AZ::u32 GetVolumeCount() = 0;

        //! Entity of the volume containing the position (invalid when none does).
        virtual AZ::EntityId FindVolumeAt(const AZ::Vector3& position) = 0;
    };
    using VoxelNavRequestBus = AZ::EBus<VoxelNavRequests>;

    //! Notifications sent by a Voxel Nav Volume (addressed by its entity).
    class VoxelNavNotifications
        : public AZ::ComponentBus
    {
    public:
        //! The volume finished (re)baking and can answer path queries.
        virtual void OnNavVolumeBaked() {}
    };
    using VoxelNavNotificationBus = AZ::EBus<VoxelNavNotifications>;
} // namespace VoxelNav

namespace AZ
{
    AZ_TYPE_INFO_SPECIALIZE(VoxelNav::AgentMode, "{8C1E2F3A-4B5C-4D6E-9F70-0A1B2C3D4E5F}");
}
