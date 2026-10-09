/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "VoxelNavData.h"

#include <AzCore/Math/MathUtils.h>

#include <cmath>

namespace VoxelNav
{
    AgentVoxelParams AgentVoxelParams::FromWorld(const AgentParams& agent, float voxelSize)
    {
        AgentVoxelParams result;
        result.m_mode = agent.m_mode;
        const float inv = 1.0f / AZ::GetMax(voxelSize, 1e-6f);
        result.m_radiusVoxels = AZ::GetMax(0.0f, agent.m_radius * inv);
        // Round the body height so an agent of exactly N voxels does not claim N+1 (float noise).
        result.m_heightVoxels = AZ::GetMax(1, static_cast<AZ::s32>(std::ceil(agent.m_height * inv - 1e-3f)));
        result.m_stepUpVoxels = AZ::GetMax(0, static_cast<AZ::s32>(std::floor(agent.m_maxStepUp * inv + 1e-3f)));
        result.m_stepDownVoxels = AZ::GetMax(0, static_cast<AZ::s32>(std::floor(agent.m_maxStepDown * inv + 1e-3f)));
        return result;
    }

    void NavData::Build(const VoxelGrid& grid, const AgentParams& agent)
    {
        m_grid = &grid;
        m_agent = AgentVoxelParams::FromWorld(agent, grid.VoxelSize());
        if (!grid.IsValid())
        {
            m_blocked.Resize(0, false);
            m_navigable.Resize(0, false);
            return;
        }
        BuildBlocked();
        BuildNavigable();
    }

    void NavData::Clear()
    {
        m_grid = nullptr;
        m_blocked.Resize(0, false);
        m_navigable.Resize(0, false);
    }

    bool NavData::IsColumnFree(const GridCoord& feet) const
    {
        if (m_agent.m_mode == AgentMode::Fly)
        {
            return !IsBlocked(feet);
        }
        for (AZ::s32 k = 0; k < m_agent.m_heightVoxels; ++k)
        {
            if (IsBlocked(GridCoord(feet.m_x, feet.m_y, feet.m_z + k)))
            {
                return false;
            }
        }
        return true;
    }

    namespace
    {
        // Distance (in voxel units) from the center of a voxel to the box of a voxel offset by (i, j, k).
        float CenterToOffsetBoxDistance(AZ::s32 i, AZ::s32 j, AZ::s32 k)
        {
            auto axis = [](AZ::s32 d)
            {
                const float a = static_cast<float>(d < 0 ? -d : d) - 0.5f;
                return a > 0.0f ? a : 0.0f;
            };
            const float dx = axis(i);
            const float dy = axis(j);
            const float dz = axis(k);
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }
    } // namespace

    void NavData::BuildBlocked()
    {
        const AZ::u64 count = m_grid->VoxelCount();
        m_blocked.Resize(count, false);

        // Every solid voxel is blocked.
        for (AZ::u64 i = 0; i < count; ++i)
        {
            if (m_grid->SolidBits().Test(i))
            {
                m_blocked.Set(i, true);
            }
        }

        // Kernel of offsets whose voxel box lies within the agent radius of a voxel center. An agent centered
        // in voxel c collides with solid voxel s exactly when dist(center(c), box(s)) < radius, so marking
        // c blocked for every solid s with offset in the kernel is exact for a sphere (Fly) / disc (Walk) agent.
        const float radius = m_agent.m_radiusVoxels;
        const AZ::s32 reach = static_cast<AZ::s32>(std::ceil(radius + 0.5f));
        const bool planar = m_agent.m_mode == AgentMode::Walk;
        AZStd::vector<GridCoord> kernel;
        for (AZ::s32 k = planar ? 0 : -reach; k <= (planar ? 0 : reach); ++k)
        {
            for (AZ::s32 j = -reach; j <= reach; ++j)
            {
                for (AZ::s32 i = -reach; i <= reach; ++i)
                {
                    if (i == 0 && j == 0 && k == 0)
                    {
                        continue;
                    }
                    if (CenterToOffsetBoxDistance(i, j, k) < radius)
                    {
                        kernel.emplace_back(i, j, k);
                    }
                }
            }
        }
        if (kernel.empty())
        {
            return;
        }

        // Only solid voxels on the surface of a solid region can block new voxels; interior ones are
        // already surrounded by solids that cover the same kernel.
        static const GridCoord faceNeighbours[6] = {
            GridCoord(1, 0, 0), GridCoord(-1, 0, 0), GridCoord(0, 1, 0), GridCoord(0, -1, 0), GridCoord(0, 0, 1), GridCoord(0, 0, -1),
        };
        for (AZ::u64 i = 0; i < count; ++i)
        {
            if (!m_grid->SolidBits().Test(i))
            {
                continue;
            }
            const GridCoord c = m_grid->Coord(i);
            bool surface = false;
            for (const GridCoord& n : faceNeighbours)
            {
                if (!m_grid->IsSolid(c + n))
                {
                    surface = true;
                    break;
                }
            }
            if (!surface)
            {
                continue;
            }
            for (const GridCoord& offset : kernel)
            {
                const GridCoord target = c + offset;
                if (m_grid->InBounds(target))
                {
                    m_blocked.Set(m_grid->Index(target), true);
                }
            }
        }
    }

    void NavData::BuildNavigable()
    {
        const AZ::u64 count = m_grid->VoxelCount();
        m_navigable.Resize(count, false);

        if (m_agent.m_mode == AgentMode::Fly)
        {
            for (AZ::u64 i = 0; i < count; ++i)
            {
                m_navigable.Set(i, !m_blocked.Test(i));
            }
            return;
        }

        const AZ::s32 sizeX = m_grid->SizeX();
        const AZ::s32 sizeY = m_grid->SizeY();
        const AZ::s32 sizeZ = m_grid->SizeZ();
        for (AZ::s32 z = 1; z < sizeZ; ++z)
        {
            for (AZ::s32 y = 0; y < sizeY; ++y)
            {
                for (AZ::s32 x = 0; x < sizeX; ++x)
                {
                    const GridCoord feet(x, y, z);
                    // Feet need raw solid support directly below; the body column must be clear of (dilated) collision.
                    if (!m_grid->IsSolid(GridCoord(x, y, z - 1)))
                    {
                        continue;
                    }
                    if (z + m_agent.m_heightVoxels > sizeZ)
                    {
                        continue;
                    }
                    if (IsColumnFree(feet))
                    {
                        m_navigable.Set(m_grid->Index(feet), true);
                    }
                }
            }
        }
    }
} // namespace VoxelNav
