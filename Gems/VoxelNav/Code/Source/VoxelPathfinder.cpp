/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "VoxelPathfinder.h"

#include <AzCore/std/algorithm.h>
#include <AzCore/std/sort.h>

#include <cmath>
#include <limits>

namespace VoxelNav
{
    const char* ToString(PathStatus status)
    {
        switch (status)
        {
        case PathStatus::Success:
            return "Success";
        case PathStatus::NotReady:
            return "NotReady";
        case PathStatus::StartNotNavigable:
            return "StartNotNavigable";
        case PathStatus::GoalNotNavigable:
            return "GoalNotNavigable";
        case PathStatus::NoPath:
            return "NoPath";
        case PathStatus::SearchLimit:
            return "SearchLimit";
        }
        return "Unknown";
    }

    namespace
    {
        constexpr AZ::u32 InvalidIndex = std::numeric_limits<AZ::u32>::max();

        float Distance(const GridCoord& a, const GridCoord& b)
        {
            const float dx = static_cast<float>(a.m_x - b.m_x);
            const float dy = static_cast<float>(a.m_y - b.m_y);
            const float dz = static_cast<float>(a.m_z - b.m_z);
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }

        struct OpenGreater
        {
            template<typename T>
            bool operator()(const T& a, const T& b) const
            {
                return a.m_f > b.m_f;
            }
        };
    } // namespace

    void VoxelPathfinder::SetNavData(const NavData* nav)
    {
        m_nav = nav;
        m_gen.clear();
        m_g.clear();
        m_parent.clear();
        m_open.clear();
        m_generation = 0;
        if (m_nav && m_nav->IsReady())
        {
            const size_t count = static_cast<size_t>(m_nav->Grid()->VoxelCount());
            m_gen.assign(count, 0);
            m_g.assign(count, 0.0f);
            m_parent.assign(count, InvalidIndex);
        }
    }

    void VoxelPathfinder::NextGeneration()
    {
        // Even = open, odd = closed for the current query; wrap by clearing the stamps.
        if (m_generation >= std::numeric_limits<AZ::u32>::max() - 4)
        {
            AZStd::fill(m_gen.begin(), m_gen.end(), 0u);
            m_generation = 0;
        }
        m_generation += 2;
    }

    bool VoxelPathfinder::CanStep(const GridCoord& from, const GridCoord& to) const
    {
        const GridCoord d = to - from;
        const AZ::s32 ax = d.m_x < 0 ? -d.m_x : d.m_x;
        const AZ::s32 ay = d.m_y < 0 ? -d.m_y : d.m_y;
        const AZ::s32 az = d.m_z < 0 ? -d.m_z : d.m_z;
        if (ax > 1 || ay > 1 || (ax == 0 && ay == 0 && az == 0))
        {
            return false;
        }
        if (!m_nav->IsNavigable(to))
        {
            return false;
        }

        const AgentVoxelParams& agent = m_nav->Agent();
        if (agent.m_mode == AgentMode::Fly)
        {
            if (az > 1)
            {
                return false;
            }
            // No corner cutting: every lower-order move that composes this diagonal must be free too.
            const AZ::s32 axes = ax + ay + az;
            if (axes >= 2)
            {
                if ((d.m_x != 0 && m_nav->IsBlocked(from + GridCoord(d.m_x, 0, 0))) ||
                    (d.m_y != 0 && m_nav->IsBlocked(from + GridCoord(0, d.m_y, 0))) ||
                    (d.m_z != 0 && m_nav->IsBlocked(from + GridCoord(0, 0, d.m_z))))
                {
                    return false;
                }
            }
            if (axes == 3)
            {
                if (m_nav->IsBlocked(from + GridCoord(d.m_x, d.m_y, 0)) || m_nav->IsBlocked(from + GridCoord(d.m_x, 0, d.m_z)) ||
                    m_nav->IsBlocked(from + GridCoord(0, d.m_y, d.m_z)))
                {
                    return false;
                }
            }
            return true;
        }

        // Walk: a horizontal move with an optional change of floor height.
        if (ax == 0 && ay == 0)
        {
            return false;
        }
        if (d.m_z > agent.m_stepUpVoxels || -d.m_z > agent.m_stepDownVoxels)
        {
            return false;
        }
        if (d.m_z > 0)
        {
            // Stepping up: the body must fit at the new height before moving over.
            if (!m_nav->IsColumnFree(GridCoord(from.m_x, from.m_y, to.m_z)))
            {
                return false;
            }
        }
        else if (d.m_z < 0)
        {
            // Dropping: the body passes over the edge at the old height before falling.
            if (!m_nav->IsColumnFree(GridCoord(to.m_x, to.m_y, from.m_z)))
            {
                return false;
            }
        }
        if (ax == 1 && ay == 1)
        {
            // Diagonal: both side columns must be free at the old and the new height.
            if (!m_nav->IsColumnFree(GridCoord(from.m_x + d.m_x, from.m_y, from.m_z)) ||
                !m_nav->IsColumnFree(GridCoord(from.m_x, from.m_y + d.m_y, from.m_z)) ||
                !m_nav->IsColumnFree(GridCoord(from.m_x + d.m_x, from.m_y, to.m_z)) ||
                !m_nav->IsColumnFree(GridCoord(from.m_x, from.m_y + d.m_y, to.m_z)))
            {
                return false;
            }
        }
        return true;
    }

    PathStatus VoxelPathfinder::FindPath(const GridCoord& start, const GridCoord& goal, AZ::u32 maxNodes, AZStd::vector<GridCoord>& outPath)
    {
        outPath.clear();
        m_lastExpanded = 0;
        if (!m_nav || !m_nav->IsReady() || m_gen.size() != static_cast<size_t>(m_nav->Grid()->VoxelCount()))
        {
            return PathStatus::NotReady;
        }
        if (!m_nav->IsNavigable(start))
        {
            return PathStatus::StartNotNavigable;
        }
        if (!m_nav->IsNavigable(goal))
        {
            return PathStatus::GoalNotNavigable;
        }

        const VoxelGrid& grid = *m_nav->Grid();
        const AZ::u32 startIndex = static_cast<AZ::u32>(grid.Index(start));
        const AZ::u32 goalIndex = static_cast<AZ::u32>(grid.Index(goal));
        if (startIndex == goalIndex)
        {
            outPath.push_back(start);
            return PathStatus::Success;
        }

        NextGeneration();
        const AZ::u32 openGen = m_generation;
        const AZ::u32 closedGen = m_generation + 1;

        const AgentVoxelParams& agent = m_nav->Agent();
        AZ::s32 minDz = -1;
        AZ::s32 maxDz = 1;
        if (agent.m_mode == AgentMode::Walk)
        {
            minDz = -agent.m_stepDownVoxels;
            maxDz = agent.m_stepUpVoxels;
        }

        m_open.clear();
        m_gen[startIndex] = openGen;
        m_g[startIndex] = 0.0f;
        m_parent[startIndex] = InvalidIndex;
        m_open.push_back({ Distance(start, goal), 0.0f, startIndex });

        while (!m_open.empty())
        {
            AZStd::pop_heap(m_open.begin(), m_open.end(), OpenGreater());
            const OpenEntry current = m_open.back();
            m_open.pop_back();

            if (m_gen[current.m_index] == closedGen || current.m_g > m_g[current.m_index])
            {
                continue; // stale entry
            }
            m_gen[current.m_index] = closedGen;
            ++m_lastExpanded;

            if (current.m_index == goalIndex)
            {
                for (AZ::u32 index = goalIndex; index != InvalidIndex; index = m_parent[index])
                {
                    outPath.push_back(grid.Coord(index));
                }
                AZStd::reverse(outPath.begin(), outPath.end());
                return PathStatus::Success;
            }
            if (maxNodes != 0 && m_lastExpanded >= maxNodes)
            {
                return PathStatus::SearchLimit;
            }

            const GridCoord c = grid.Coord(current.m_index);
            for (AZ::s32 dz = minDz; dz <= maxDz; ++dz)
            {
                for (AZ::s32 dy = -1; dy <= 1; ++dy)
                {
                    for (AZ::s32 dx = -1; dx <= 1; ++dx)
                    {
                        if (dx == 0 && dy == 0 && (dz == 0 || agent.m_mode == AgentMode::Walk))
                        {
                            continue;
                        }
                        const GridCoord n(c.m_x + dx, c.m_y + dy, c.m_z + dz);
                        if (!grid.InBounds(n))
                        {
                            continue;
                        }
                        const AZ::u32 nIndex = static_cast<AZ::u32>(grid.Index(n));
                        if (m_gen[nIndex] == closedGen)
                        {
                            continue;
                        }
                        if (!CanStep(c, n))
                        {
                            continue;
                        }
                        const float ng = current.m_g + Distance(c, n);
                        if (m_gen[nIndex] == openGen && ng >= m_g[nIndex])
                        {
                            continue;
                        }
                        m_gen[nIndex] = openGen;
                        m_g[nIndex] = ng;
                        m_parent[nIndex] = current.m_index;
                        m_open.push_back({ ng + Distance(n, goal), ng, nIndex });
                        AZStd::push_heap(m_open.begin(), m_open.end(), OpenGreater());
                    }
                }
            }
        }
        return PathStatus::NoPath;
    }

    bool VoxelPathfinder::FindNearestNavigable(const GridCoord& from, AZ::s32 maxRadius, GridCoord& out) const
    {
        if (!m_nav || !m_nav->IsReady())
        {
            return false;
        }
        if (m_nav->IsNavigable(from))
        {
            out = from;
            return true;
        }
        float bestDistance = std::numeric_limits<float>::max();
        bool found = false;
        for (AZ::s32 r = 1; r <= maxRadius; ++r)
        {
            // A later shell can only win while its Chebyshev radius is below the best Euclidean distance.
            if (found && static_cast<float>(r) >= bestDistance)
            {
                break;
            }
            for (AZ::s32 k = -r; k <= r; ++k)
            {
                for (AZ::s32 j = -r; j <= r; ++j)
                {
                    for (AZ::s32 i = -r; i <= r; ++i)
                    {
                        const AZ::s32 cheb = AZStd::max(AZStd::max(i < 0 ? -i : i, j < 0 ? -j : j), k < 0 ? -k : k);
                        if (cheb != r)
                        {
                            continue;
                        }
                        const GridCoord c = from + GridCoord(i, j, k);
                        if (!m_nav->IsNavigable(c))
                        {
                            continue;
                        }
                        const float distance = Distance(from, c);
                        if (distance < bestDistance)
                        {
                            bestDistance = distance;
                            out = c;
                            found = true;
                        }
                    }
                }
            }
        }
        return found;
    }

    bool VoxelPathfinder::TraversableForLineOfSight(const GridCoord& coord) const
    {
        if (!m_nav->IsColumnFree(coord))
        {
            return false;
        }
        if (m_nav->Agent().m_mode == AgentMode::Fly)
        {
            return true;
        }
        // Walking: the segment may float over a floor the agent can step onto, never over a gap or a drop
        // deeper than the step-down limit.
        for (AZ::s32 k = 0; k <= m_nav->Agent().m_stepDownVoxels; ++k)
        {
            if (m_nav->IsNavigable(GridCoord(coord.m_x, coord.m_y, coord.m_z - k)))
            {
                return true;
            }
        }
        return false;
    }

    bool VoxelPathfinder::HasLineOfSight(const GridCoord& a, const GridCoord& b) const
    {
        if (!m_nav || !m_nav->IsReady())
        {
            return false;
        }
        if (a == b)
        {
            return TraversableForLineOfSight(a);
        }

        // Amanatides & Woo voxel traversal from center(a) to center(b).
        const float dir[3] = {
            static_cast<float>(b.m_x - a.m_x),
            static_cast<float>(b.m_y - a.m_y),
            static_cast<float>(b.m_z - a.m_z),
        };
        AZ::s32 cur[3] = { a.m_x, a.m_y, a.m_z };
        const AZ::s32 end[3] = { b.m_x, b.m_y, b.m_z };
        AZ::s32 step[3];
        float tMax[3];
        float tDelta[3];
        for (int i = 0; i < 3; ++i)
        {
            if (dir[i] > 0.0f)
            {
                step[i] = 1;
                tDelta[i] = 1.0f / dir[i];
                tMax[i] = 0.5f * tDelta[i];
            }
            else if (dir[i] < 0.0f)
            {
                step[i] = -1;
                tDelta[i] = -1.0f / dir[i];
                tMax[i] = 0.5f * tDelta[i];
            }
            else
            {
                step[i] = 0;
                tDelta[i] = std::numeric_limits<float>::max();
                tMax[i] = std::numeric_limits<float>::max();
            }
        }

        const AZ::s32 maxSteps = (end[0] - cur[0] < 0 ? cur[0] - end[0] : end[0] - cur[0]) +
            (end[1] - cur[1] < 0 ? cur[1] - end[1] : end[1] - cur[1]) + (end[2] - cur[2] < 0 ? cur[2] - end[2] : end[2] - cur[2]) + 1;
        for (AZ::s32 n = 0; n <= maxSteps; ++n)
        {
            const GridCoord c(cur[0], cur[1], cur[2]);
            if (!TraversableForLineOfSight(c))
            {
                return false;
            }
            if (cur[0] == end[0] && cur[1] == end[1] && cur[2] == end[2])
            {
                return true;
            }
            int axis = 0;
            if (tMax[1] < tMax[axis])
            {
                axis = 1;
            }
            if (tMax[2] < tMax[axis])
            {
                axis = 2;
            }
            // When the segment crosses an edge or corner exactly, several axes advance at once; the agent
            // then touches the voxels on both sides, so require them to be traversable as well.
            for (int other = 0; other < 3; ++other)
            {
                if (other != axis && step[other] != 0 && std::fabs(tMax[other] - tMax[axis]) < 1e-5f)
                {
                    GridCoord side = c;
                    (&side.m_x)[other] += step[other];
                    if (!TraversableForLineOfSight(side))
                    {
                        return false;
                    }
                }
            }
            cur[axis] += step[axis];
            tMax[axis] += tDelta[axis];
        }
        return false;
    }

    void VoxelPathfinder::SmoothPath(const AZStd::vector<GridCoord>& path, AZStd::vector<GridCoord>& outPath) const
    {
        outPath.clear();
        if (path.empty())
        {
            return;
        }
        outPath.push_back(path.front());
        size_t anchor = 0;
        while (anchor + 1 < path.size())
        {
            size_t next = path.size() - 1;
            while (next > anchor + 1 && !HasLineOfSight(path[anchor], path[next]))
            {
                --next;
            }
            outPath.push_back(path[next]);
            anchor = next;
        }
    }
} // namespace VoxelNav
