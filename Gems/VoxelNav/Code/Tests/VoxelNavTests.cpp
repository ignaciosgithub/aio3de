/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/UnitTest/TestTypes.h>
#include <AzTest/AzTest.h>

#include <VoxelGrid.h>
#include <VoxelNavData.h>
#include <VoxelPathfinder.h>

namespace VoxelNav::Tests
{
    class VoxelNavFixture : public UnitTest::LeakDetectionFixture
    {
    protected:
        // Grid of sx*sy*sz unit voxels with its minimum corner at the origin; optional solid floor at z = 0.
        void MakeGrid(AZ::s32 sx, AZ::s32 sy, AZ::s32 sz, bool floor)
        {
            const AZ::Aabb bounds = AZ::Aabb::CreateFromMinMax(
                AZ::Vector3::CreateZero(), AZ::Vector3(static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(sz)));
            ASSERT_TRUE(m_grid.Initialize(bounds, 1.0f));
            if (floor)
            {
                for (AZ::s32 y = 0; y < sy; ++y)
                {
                    for (AZ::s32 x = 0; x < sx; ++x)
                    {
                        m_grid.SetSolid(GridCoord(x, y, 0), true);
                    }
                }
            }
        }

        void FillSolid(AZ::s32 x0, AZ::s32 y0, AZ::s32 z0, AZ::s32 x1, AZ::s32 y1, AZ::s32 z1)
        {
            for (AZ::s32 z = z0; z <= z1; ++z)
            {
                for (AZ::s32 y = y0; y <= y1; ++y)
                {
                    for (AZ::s32 x = x0; x <= x1; ++x)
                    {
                        m_grid.SetSolid(GridCoord(x, y, z), true);
                    }
                }
            }
        }

        void Bake(AgentMode mode, float radius = 0.3f, float height = 1.0f, float stepUp = 1.0f, float stepDown = 1.0f)
        {
            AgentParams agent;
            agent.m_mode = mode;
            agent.m_radius = radius;
            agent.m_height = height;
            agent.m_maxStepUp = stepUp;
            agent.m_maxStepDown = stepDown;
            m_nav.Build(m_grid, agent);
            m_finder.SetNavData(&m_nav);
        }

        // Every waypoint must be navigable and every move must be a legal step.
        void ExpectValidPath(const AZStd::vector<GridCoord>& path, const GridCoord& start, const GridCoord& goal)
        {
            ASSERT_FALSE(path.empty());
            EXPECT_EQ(path.front(), start);
            EXPECT_EQ(path.back(), goal);
            for (size_t i = 0; i < path.size(); ++i)
            {
                EXPECT_TRUE(m_nav.IsNavigable(path[i])) << "waypoint " << i;
                if (i > 0)
                {
                    EXPECT_TRUE(m_finder.CanStep(path[i - 1], path[i])) << "step " << i;
                }
            }
        }

        VoxelGrid m_grid;
        NavData m_nav;
        VoxelPathfinder m_finder;
        AZStd::vector<GridCoord> m_path;
    };

    TEST_F(VoxelNavFixture, Grid_WorldVoxelConversionsRoundTrip)
    {
        const AZ::Aabb bounds = AZ::Aabb::CreateFromMinMax(AZ::Vector3(-5.0f, -5.0f, 0.0f), AZ::Vector3(5.0f, 5.0f, 2.0f));
        ASSERT_TRUE(m_grid.Initialize(bounds, 0.5f));
        EXPECT_EQ(m_grid.SizeX(), 20);
        EXPECT_EQ(m_grid.SizeY(), 20);
        EXPECT_EQ(m_grid.SizeZ(), 4);
        EXPECT_EQ(m_grid.VoxelCount(), 1600u);

        const GridCoord c = m_grid.WorldToVoxel(AZ::Vector3(-4.9f, 4.9f, 1.1f));
        EXPECT_EQ(c, GridCoord(0, 19, 2));
        EXPECT_EQ(m_grid.WorldToVoxel(m_grid.VoxelCenter(c)), c);
        EXPECT_EQ(m_grid.Coord(m_grid.Index(c)), c);
        EXPECT_TRUE(m_grid.VoxelBottomCenter(c).IsClose(AZ::Vector3(-4.75f, 4.75f, 1.0f)));
        EXPECT_FALSE(m_grid.InBounds(GridCoord(20, 0, 0)));
        EXPECT_FALSE(m_grid.InBounds(m_grid.WorldToVoxel(AZ::Vector3(100.0f, 0.0f, 0.0f))));
    }

    TEST_F(VoxelNavFixture, Grid_RejectsDegenerateAndOversizedGrids)
    {
        EXPECT_FALSE(m_grid.Initialize(AZ::Aabb::CreateNull(), 1.0f));
        EXPECT_FALSE(m_grid.Initialize(AZ::Aabb::CreateFromMinMax(AZ::Vector3::CreateZero(), AZ::Vector3(1.0f)), 0.0f));
        EXPECT_FALSE(m_grid.Initialize(AZ::Aabb::CreateFromMinMax(AZ::Vector3::CreateZero(), AZ::Vector3(10000.0f)), 0.1f));
        EXPECT_FALSE(m_grid.IsValid());
    }

    TEST_F(VoxelNavFixture, Fly_OpenSpaceIsAStraightDiagonalAndSmoothsToTwoPoints)
    {
        MakeGrid(10, 10, 10, false);
        Bake(AgentMode::Fly, 0.3f);
        const GridCoord start(0, 0, 0);
        const GridCoord goal(9, 9, 9);
        EXPECT_EQ(m_finder.FindPath(start, goal, 0, m_path), PathStatus::Success);
        ExpectValidPath(m_path, start, goal);
        EXPECT_EQ(m_path.size(), 10u); // one 3-axis diagonal step per voxel

        AZStd::vector<GridCoord> smooth;
        m_finder.SmoothPath(m_path, smooth);
        ASSERT_EQ(smooth.size(), 2u);
        EXPECT_EQ(smooth.front(), start);
        EXPECT_EQ(smooth.back(), goal);
    }

    TEST_F(VoxelNavFixture, Fly_PathGoesThroughTheOnlyGapInAWall)
    {
        MakeGrid(9, 9, 5, false);
        FillSolid(4, 0, 0, 4, 8, 4); // wall at x = 4
        m_grid.SetSolid(GridCoord(4, 4, 2), false); // single hole
        Bake(AgentMode::Fly, 0.3f);
        const GridCoord start(0, 0, 0);
        const GridCoord goal(8, 8, 4);
        EXPECT_EQ(m_finder.FindPath(start, goal, 0, m_path), PathStatus::Success);
        ExpectValidPath(m_path, start, goal);
        bool throughHole = false;
        for (const GridCoord& c : m_path)
        {
            if (c.m_x == 4)
            {
                EXPECT_EQ(c, GridCoord(4, 4, 2));
                throughHole = true;
            }
        }
        EXPECT_TRUE(throughHole);
    }

    TEST_F(VoxelNavFixture, Fly_SolidWallMeansNoPath)
    {
        MakeGrid(6, 6, 6, false);
        FillSolid(3, 0, 0, 3, 5, 5);
        Bake(AgentMode::Fly, 0.3f);
        EXPECT_EQ(m_finder.FindPath(GridCoord(0, 0, 0), GridCoord(5, 5, 5), 0, m_path), PathStatus::NoPath);
        EXPECT_TRUE(m_path.empty());
    }

    TEST_F(VoxelNavFixture, Fly_DoesNotCutCorners)
    {
        MakeGrid(3, 3, 1, false);
        m_grid.SetSolid(GridCoord(1, 0, 0), true);
        m_grid.SetSolid(GridCoord(0, 1, 0), true);
        Bake(AgentMode::Fly, 0.3f);
        // (0,0) and (1,1) are both free but the diagonal squeezes between two solids.
        EXPECT_TRUE(m_nav.IsNavigable(GridCoord(0, 0, 0)));
        EXPECT_TRUE(m_nav.IsNavigable(GridCoord(1, 1, 0)));
        EXPECT_FALSE(m_finder.CanStep(GridCoord(0, 0, 0), GridCoord(1, 1, 0)));
        EXPECT_EQ(m_finder.FindPath(GridCoord(0, 0, 0), GridCoord(2, 2, 0), 0, m_path), PathStatus::NoPath);
    }

    TEST_F(VoxelNavFixture, Fly_AgentRadiusBlocksNarrowCorridors)
    {
        // Corridor one voxel wide along x at y = 2, z = 2, walled on all four sides.
        MakeGrid(8, 5, 5, false);
        FillSolid(0, 0, 0, 7, 4, 4);
        for (AZ::s32 x = 0; x < 8; ++x)
        {
            m_grid.SetSolid(GridCoord(x, 2, 2), false);
        }
        Bake(AgentMode::Fly, 0.3f);
        EXPECT_EQ(m_finder.FindPath(GridCoord(0, 2, 2), GridCoord(7, 2, 2), 0, m_path), PathStatus::Success);
        EXPECT_EQ(m_path.size(), 8u);

        Bake(AgentMode::Fly, 0.6f); // wider than the half-voxel clearance available
        EXPECT_FALSE(m_nav.IsNavigable(GridCoord(3, 2, 2)));
        EXPECT_EQ(m_finder.FindPath(GridCoord(0, 2, 2), GridCoord(7, 2, 2), 0, m_path), PathStatus::StartNotNavigable);
    }

    TEST_F(VoxelNavFixture, Fly_SearchLimitIsReported)
    {
        MakeGrid(20, 20, 20, false);
        Bake(AgentMode::Fly, 0.3f);
        EXPECT_EQ(m_finder.FindPath(GridCoord(0, 0, 0), GridCoord(19, 19, 19), 5, m_path), PathStatus::SearchLimit);
        EXPECT_EQ(m_finder.GetLastExpandedCount(), 5u);
    }

    TEST_F(VoxelNavFixture, Walk_FlatFloorPathStaysOnTheFloorAndSmoothsToTwoPoints)
    {
        MakeGrid(10, 10, 4, true);
        Bake(AgentMode::Walk, 0.3f, 1.0f);
        EXPECT_FALSE(m_nav.IsNavigable(GridCoord(0, 0, 0))); // inside the floor
        EXPECT_TRUE(m_nav.IsNavigable(GridCoord(0, 0, 1)));
        EXPECT_FALSE(m_nav.IsNavigable(GridCoord(0, 0, 2))); // in the air
        const GridCoord start(0, 0, 1);
        const GridCoord goal(9, 3, 1);
        EXPECT_EQ(m_finder.FindPath(start, goal, 0, m_path), PathStatus::Success);
        ExpectValidPath(m_path, start, goal);
        EXPECT_EQ(m_path.size(), 10u);
        for (const GridCoord& c : m_path)
        {
            EXPECT_EQ(c.m_z, 1);
        }
        AZStd::vector<GridCoord> smooth;
        m_finder.SmoothPath(m_path, smooth);
        EXPECT_EQ(smooth.size(), 2u);
    }

    TEST_F(VoxelNavFixture, Walk_StepsOntoLedgesWithinTheStepLimitOnly)
    {
        MakeGrid(10, 3, 5, true);
        FillSolid(5, 0, 1, 9, 2, 1); // raised platform, 1 voxel higher, on the right half
        Bake(AgentMode::Walk, 0.3f, 1.0f, /*stepUp*/ 1.0f);
        const GridCoord start(0, 1, 1);
        const GridCoord goal(9, 1, 2);
        EXPECT_EQ(m_finder.FindPath(start, goal, 0, m_path), PathStatus::Success);
        ExpectValidPath(m_path, start, goal);

        Bake(AgentMode::Walk, 0.3f, 1.0f, /*stepUp*/ 0.0f, /*stepDown*/ 0.0f);
        EXPECT_EQ(m_finder.FindPath(start, goal, 0, m_path), PathStatus::NoPath);
    }

    TEST_F(VoxelNavFixture, Walk_CanDropButNotClimbATallLedge)
    {
        MakeGrid(10, 3, 6, true);
        FillSolid(5, 0, 1, 9, 2, 2); // platform 2 voxels higher
        Bake(AgentMode::Walk, 0.3f, 1.0f, /*stepUp*/ 1.0f, /*stepDown*/ 2.0f);
        EXPECT_EQ(m_finder.FindPath(GridCoord(9, 1, 3), GridCoord(0, 1, 1), 0, m_path), PathStatus::Success);
        EXPECT_EQ(m_finder.FindPath(GridCoord(0, 1, 1), GridCoord(9, 1, 3), 0, m_path), PathStatus::NoPath);
    }

    TEST_F(VoxelNavFixture, Walk_GapsCannotBeCrossedAndSmoothingNeverCutsOverThem)
    {
        // Floor with a one-voxel-wide pit across the middle, bridged only at y = 0.
        MakeGrid(9, 5, 4, true);
        for (AZ::s32 y = 1; y < 5; ++y)
        {
            m_grid.SetSolid(GridCoord(4, y, 0), false);
        }
        Bake(AgentMode::Walk, 0.3f, 1.0f, 1.0f, /*stepDown*/ 0.0f);
        const GridCoord start(0, 4, 1);
        const GridCoord goal(8, 4, 1);
        EXPECT_EQ(m_finder.FindPath(start, goal, 0, m_path), PathStatus::Success);
        ExpectValidPath(m_path, start, goal);
        bool viaBridge = false;
        for (const GridCoord& c : m_path)
        {
            if (c.m_x == 4)
            {
                EXPECT_EQ(c.m_y, 0);
                viaBridge = true;
            }
        }
        EXPECT_TRUE(viaBridge);

        EXPECT_FALSE(m_finder.HasLineOfSight(GridCoord(3, 4, 1), GridCoord(5, 4, 1)));
        AZStd::vector<GridCoord> smooth;
        m_finder.SmoothPath(m_path, smooth);
        ASSERT_GE(smooth.size(), 3u);
        bool smoothViaBridge = false;
        for (const GridCoord& c : smooth)
        {
            smoothViaBridge = smoothViaBridge || (c.m_x == 4 && c.m_y == 0);
        }
        EXPECT_TRUE(smoothViaBridge);
    }

    TEST_F(VoxelNavFixture, Walk_AgentHeightRespectsCeilings)
    {
        MakeGrid(6, 3, 5, true);
        FillSolid(2, 0, 3, 3, 2, 3); // low ceiling over x = 2..3 leaving 2 voxels of headroom (z = 1, 2)
        Bake(AgentMode::Walk, 0.3f, /*height*/ 2.0f);
        EXPECT_TRUE(m_nav.IsNavigable(GridCoord(2, 1, 1)));
        EXPECT_EQ(m_finder.FindPath(GridCoord(0, 1, 1), GridCoord(5, 1, 1), 0, m_path), PathStatus::Success);

        Bake(AgentMode::Walk, 0.3f, /*height*/ 3.0f);
        EXPECT_FALSE(m_nav.IsNavigable(GridCoord(2, 1, 1)));
        EXPECT_EQ(m_finder.FindPath(GridCoord(0, 1, 1), GridCoord(5, 1, 1), 0, m_path), PathStatus::NoPath);
    }

    TEST_F(VoxelNavFixture, Walk_RadiusKeepsTheAgentAwayFromWalls)
    {
        MakeGrid(6, 6, 4, true);
        FillSolid(3, 0, 1, 3, 5, 3); // thin wall along y at x = 3
        Bake(AgentMode::Walk, 0.3f);
        EXPECT_TRUE(m_nav.IsNavigable(GridCoord(2, 2, 1)));
        Bake(AgentMode::Walk, 0.6f);
        EXPECT_FALSE(m_nav.IsNavigable(GridCoord(2, 2, 1)));
        EXPECT_TRUE(m_nav.IsNavigable(GridCoord(1, 2, 1)));
    }

    TEST_F(VoxelNavFixture, NearestNavigable_SnapsAPointInTheAirDownToTheFloor)
    {
        MakeGrid(6, 6, 6, true);
        Bake(AgentMode::Walk, 0.3f);
        GridCoord snapped;
        EXPECT_TRUE(m_finder.FindNearestNavigable(GridCoord(2, 2, 4), 4, snapped));
        EXPECT_EQ(snapped, GridCoord(2, 2, 1));
        EXPECT_FALSE(m_finder.FindNearestNavigable(GridCoord(2, 2, 5), 2, snapped));
        EXPECT_TRUE(m_finder.FindNearestNavigable(GridCoord(2, 2, 1), 0, snapped));
        EXPECT_EQ(snapped, GridCoord(2, 2, 1));
    }

    TEST_F(VoxelNavFixture, NotReadyWithoutNavData)
    {
        EXPECT_EQ(m_finder.FindPath(GridCoord(0, 0, 0), GridCoord(1, 1, 1), 0, m_path), PathStatus::NotReady);
        GridCoord snapped;
        EXPECT_FALSE(m_finder.FindNearestNavigable(GridCoord(0, 0, 0), 2, snapped));
    }
} // namespace VoxelNav::Tests

AZ_UNIT_TEST_HOOK(DEFAULT_UNIT_TEST_ENV);
