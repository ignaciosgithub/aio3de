/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include "VoxelNavData.h"

namespace VoxelNav
{
    enum class PathStatus : AZ::u8
    {
        Success,
        NotReady,
        StartNotNavigable,
        GoalNotNavigable,
        NoPath,
        SearchLimit,
    };

    const char* ToString(PathStatus status);

    //! 3D A* over NavData with Euclidean step costs, no corner cutting, string-pulling smoothing.
    //! Keeps per-voxel scratch buffers between queries (generation stamped) so repeated queries do not allocate.
    class VoxelPathfinder
    {
    public:
        void SetNavData(const NavData* nav);
        const NavData* GetNavData() const
        {
            return m_nav;
        }

        //! A* from start to goal (both must be navigable). outPath receives every voxel from start to goal.
        //! maxNodes bounds the number of expanded nodes (0 = unbounded).
        PathStatus FindPath(const GridCoord& start, const GridCoord& goal, AZ::u32 maxNodes, AZStd::vector<GridCoord>& outPath);

        //! Nodes expanded by the last FindPath call.
        AZ::u32 GetLastExpandedCount() const
        {
            return m_lastExpanded;
        }

        //! Closest navigable voxel (Euclidean, voxel centers) within maxRadius voxels (Chebyshev) of from.
        bool FindNearestNavigable(const GridCoord& from, AZ::s32 maxRadius, GridCoord& out) const;

        //! True when the agent can move from one navigable voxel to an adjacent one (step limits, corner clipping).
        bool CanStep(const GridCoord& from, const GridCoord& to) const;

        //! True when a straight segment between the two voxel centers only crosses voxels the agent may traverse.
        bool HasLineOfSight(const GridCoord& a, const GridCoord& b) const;

        //! String pulling: keeps the fewest waypoints such that consecutive ones have line of sight.
        void SmoothPath(const AZStd::vector<GridCoord>& path, AZStd::vector<GridCoord>& outPath) const;

    private:
        struct OpenEntry
        {
            float m_f;
            float m_g;
            AZ::u32 m_index;
        };

        void NextGeneration();
        bool TraversableForLineOfSight(const GridCoord& coord) const;

        const NavData* m_nav = nullptr;
        AZStd::vector<AZ::u32> m_gen;
        AZStd::vector<float> m_g;
        AZStd::vector<AZ::u32> m_parent;
        AZStd::vector<OpenEntry> m_open;
        AZ::u32 m_generation = 0;
        AZ::u32 m_lastExpanded = 0;
    };
} // namespace VoxelNav
