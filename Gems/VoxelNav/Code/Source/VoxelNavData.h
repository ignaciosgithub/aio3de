/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <VoxelNav/VoxelNavBus.h>

#include "VoxelGrid.h"

namespace VoxelNav
{
    //! Agent shape and movement limits in world units.
    struct AgentParams
    {
        AgentMode m_mode = AgentMode::Walk;
        float m_radius = 0.4f;
        float m_height = 1.8f;
        float m_maxStepUp = 0.5f;
        float m_maxStepDown = 1.0f;
    };

    //! AgentParams quantized to the grid.
    struct AgentVoxelParams
    {
        AgentMode m_mode = AgentMode::Walk;
        //! Clearance radius in voxel units (continuous; the dilation kernel is derived from it).
        float m_radiusVoxels = 0.0f;
        //! Number of voxels the agent's body occupies above its feet voxel (>= 1).
        AZ::s32 m_heightVoxels = 1;
        AZ::s32 m_stepUpVoxels = 0;
        AZ::s32 m_stepDownVoxels = 0;

        static AgentVoxelParams FromWorld(const AgentParams& agent, float voxelSize);
    };

    //! Navigation layers derived from a solid voxel grid for one agent configuration:
    //! - blocked: solid voxels grown by the agent radius (Walk: per horizontal layer, Fly: sphere), so a voxel is
    //!   blocked when an agent centered in it would intersect collision;
    //! - navigable: voxels the agent may occupy (Walk: feet voxel standing on solid with a free body column,
    //!   Fly: any non-blocked voxel).
    class NavData
    {
    public:
        void Build(const VoxelGrid& grid, const AgentParams& agent);
        void Clear();

        bool IsReady() const
        {
            return m_grid != nullptr && m_grid->IsValid() && m_navigable.Size() == m_grid->VoxelCount();
        }
        const VoxelGrid* Grid() const
        {
            return m_grid;
        }
        const AgentVoxelParams& Agent() const
        {
            return m_agent;
        }

        //! Out-of-bounds voxels are blocked.
        bool IsBlocked(const GridCoord& coord) const
        {
            return !m_grid->InBounds(coord) || m_blocked.Test(m_grid->Index(coord));
        }
        //! Out-of-bounds voxels are not navigable.
        bool IsNavigable(const GridCoord& coord) const
        {
            return m_grid->InBounds(coord) && m_navigable.Test(m_grid->Index(coord));
        }
        //! Walk: the agent's body column (feet .. feet + height - 1) is free of blocked voxels. Fly: !IsBlocked.
        bool IsColumnFree(const GridCoord& feet) const;

        AZ::u64 NavigableCount() const
        {
            return m_navigable.PopCount();
        }
        AZ::u64 BlockedCount() const
        {
            return m_blocked.PopCount();
        }

    private:
        void BuildBlocked();
        void BuildNavigable();

        const VoxelGrid* m_grid = nullptr;
        AgentVoxelParams m_agent;
        VoxelBits m_blocked;
        VoxelBits m_navigable;
    };
} // namespace VoxelNav
