/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <WorldPartition/WorldPartitionStreamingPlanner.h>

#include <AzTest/AzTest.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <QApplication>

namespace UnitTest
{
    class WorldPartitionTestsEnvironment final : public AZ::Test::ITestEnvironment
    {
    protected:
        void SetupEnvironment() override
        {
            static int argc = 1;
            static char applicationName[] = "WorldPartition.Tests";
            static char* argv[] = { applicationName, nullptr };
            m_application = AZStd::make_unique<QApplication>(argc, argv);
            m_defaultEnvironment.reset(AZ::Test::DefaultTestEnv());
            m_defaultEnvironment->SetUp();
        }

        void TeardownEnvironment() override
        {
            m_defaultEnvironment->TearDown();
            m_defaultEnvironment.reset();
            m_application.reset();
        }

    private:
        AZStd::unique_ptr<QApplication> m_application;
        AZStd::unique_ptr<AZ::Test::ITestEnvironment> m_defaultEnvironment;
    };
}

namespace WorldPartition
{
    namespace
    {
        AZ::Aabb PointBounds(float x, float y = 0.0f, float z = 0.0f)
        {
            return AZ::Aabb::CreateFromPoint(AZ::Vector3(x, y, z));
        }
    }

    TEST(WorldPartitionPlannerTests, LoadsInsideRadius)
    {
        auto plan = PlanStreaming({ { PointBounds(5.0f), false } }, { { AZ::Vector3::CreateZero(), 0.0f } },
            { 10.0f, 20.0f, 2, true });
        ASSERT_EQ(plan.m_load.size(), 1);
        EXPECT_EQ(plan.m_load.front(), 0);
    }

    TEST(WorldPartitionPlannerTests, KeepsLoadedCellUntilUnloadRadius)
    {
        const AZStd::vector<StreamingSource> source{ { AZ::Vector3::CreateZero(), 0.0f } };
        auto inside = PlanStreaming({ { PointBounds(15.0f), true } }, source, { 10.0f, 20.0f, 2, true });
        EXPECT_TRUE(inside.m_unload.empty());
        auto outside = PlanStreaming({ { PointBounds(21.0f), true } }, source, { 10.0f, 20.0f, 2, true });
        ASSERT_EQ(outside.m_unload.size(), 1);
        EXPECT_EQ(outside.m_unload.front(), 0);
    }

    TEST(WorldPartitionPlannerTests, KeepsLoadedCellWithinBaseUnloadRadiusWithZeroLoadRadius)
    {
        auto plan = PlanStreaming(
            { { PointBounds(30.0f), true } }, { { AZ::Vector3::CreateZero(), 0.0f } }, { 0.0f, 50.0f, 2, true });
        EXPECT_TRUE(plan.m_unload.empty());
    }

    TEST(WorldPartitionPlannerTests, LoadsNearestCellsWithinBudget)
    {
        const AZStd::vector<CellStreamState> cells{
            { PointBounds(40.0f), false }, { PointBounds(10.0f), false }, { PointBounds(30.0f), false },
            { PointBounds(20.0f), false }, { PointBounds(50.0f), false }};
        auto plan = PlanStreaming(cells, { { AZ::Vector3::CreateZero(), 0.0f } }, { 100.0f, 120.0f, 2, true });
        ASSERT_EQ(plan.m_load.size(), 2);
        EXPECT_EQ(plan.m_load[0], 1);
        EXPECT_EQ(plan.m_load[1], 3);
    }

    TEST(WorldPartitionPlannerTests, IgnoresZInTwoDimensionalMode)
    {
        auto plan = PlanStreaming({ { PointBounds(0.0f, 0.0f, 900.0f), false } },
            { { AZ::Vector3::CreateZero(), 0.0f } }, { 1.0f, 2.0f, 1, true });
        EXPECT_EQ(plan.m_load.size(), 1);
    }

    TEST(WorldPartitionPlannerTests, EitherSourceCanLoadCell)
    {
        auto plan = PlanStreaming({ { PointBounds(15.0f), false } },
            { { AZ::Vector3::CreateZero(), 10.0f }, { AZ::Vector3(15.0f, 0.0f, 0.0f), 10.0f } },
            { 10.0f, 20.0f, 1, true });
        EXPECT_EQ(plan.m_load.size(), 1);
    }

    TEST(WorldPartitionPlannerTests, UsesPerSourceRadiusOverride)
    {
        const AZStd::vector<CellStreamState> cells{ { PointBounds(8.0f), false } };
        const AZStd::vector<StreamingSource> source{ { AZ::Vector3::CreateZero(), 10.0f } };
        EXPECT_EQ(PlanStreaming(cells, source, { 5.0f, 10.0f, 1, true }).m_load.size(), 1);
        EXPECT_TRUE(PlanStreaming(cells, { { AZ::Vector3::CreateZero(), 6.0f } },
            { 5.0f, 10.0f, 1, true }).m_load.empty());
    }

    TEST(WorldPartitionPlannerTests, NoSourcesDoesNoWork)
    {
        auto plan = PlanStreaming({ { PointBounds(0.0f), true } }, {}, { 10.0f, 20.0f, 2, true });
        EXPECT_TRUE(plan.m_load.empty());
        EXPECT_TRUE(plan.m_unload.empty());
    }

    TEST(WorldPartitionPlannerTests, MeasuresZInThreeDimensionalMode)
    {
        auto plan = PlanStreaming({ { PointBounds(0.0f, 0.0f, 10.0f), false } },
            { { AZ::Vector3::CreateZero(), 0.0f } }, { 1.0f, 2.0f, 1, false });
        EXPECT_TRUE(plan.m_load.empty());
    }

    TEST(WorldPartitionPlannerTests, ExpandedLoadedBoundsPreventUnloadOutsideGridBounds)
    {
        const AZ::Aabb gridBounds =
            AZ::Aabb::CreateFromMinMax(AZ::Vector3(0.0f, 0.0f, 0.0f), AZ::Vector3(64.0f, 64.0f, 0.0f));
        const AZ::Aabb entityBounds =
            AZ::Aabb::CreateFromMinMax(AZ::Vector3(64.0f, 0.0f, 0.0f), AZ::Vector3(114.0f, 64.0f, 0.0f));
        const AZ::Aabb streamBounds = ExpandStreamBounds(gridBounds, { entityBounds });
        const AZStd::vector<StreamingSource> sources{ { AZ::Vector3(80.0f, 32.0f, 0.0f), 0.0f } };
        const StreamingSettings settings{ 5.0f, 10.0f, 1, true };

        const StreamingPlan gridPlan = PlanStreaming({ { gridBounds, true } }, sources, settings);
        const StreamingPlan expandedPlan = PlanStreaming({ { streamBounds, true } }, sources, settings);

        ASSERT_EQ(gridPlan.m_unload.size(), 1);
        EXPECT_TRUE(expandedPlan.m_unload.empty());
        EXPECT_FLOAT_EQ(streamBounds.GetMax().GetX(), 114.0f);
    }

    TEST(WorldPartitionPlannerTests, ExpandStreamBoundsIncludesLargerEntityBounds)
    {
        const AZ::Aabb current =
            AZ::Aabb::CreateFromMinMax(AZ::Vector3::CreateZero(), AZ::Vector3(1.0f));
        const AZ::Aabb larger =
            AZ::Aabb::CreateFromMinMax(AZ::Vector3(-2.0f), AZ::Vector3(5.0f));

        const AZ::Aabb expanded = ExpandStreamBounds(current, { larger });

        EXPECT_EQ(expanded.GetMin(), larger.GetMin());
        EXPECT_EQ(expanded.GetMax(), larger.GetMax());
    }

    TEST(WorldPartitionPlannerTests, ExpandStreamBoundsIgnoresInvalidEntityBounds)
    {
        const AZ::Aabb current =
            AZ::Aabb::CreateFromMinMax(AZ::Vector3::CreateZero(), AZ::Vector3(1.0f));

        const AZ::Aabb expanded = ExpandStreamBounds(current, { AZ::Aabb::CreateNull() });

        EXPECT_EQ(expanded, current);
    }

    TEST(WorldPartitionPlannerTests, ExpandStreamBoundsNeverShrinks)
    {
        const AZ::Aabb current =
            AZ::Aabb::CreateFromMinMax(AZ::Vector3::CreateZero(), AZ::Vector3(10.0f));
        const AZ::Aabb smaller =
            AZ::Aabb::CreateFromMinMax(AZ::Vector3(2.0f), AZ::Vector3(4.0f));

        const AZ::Aabb expanded = ExpandStreamBounds(current, { smaller });

        EXPECT_EQ(expanded, current);
    }

    TEST(WorldPartitionPlannerTests, ExpandStreamBoundsReturnsCurrentForEmptyBounds)
    {
        const AZ::Aabb current =
            AZ::Aabb::CreateFromMinMax(AZ::Vector3::CreateZero(), AZ::Vector3(1.0f));

        const AZ::Aabb expanded = ExpandStreamBounds(current, {});

        EXPECT_EQ(expanded, current);
    }
}

AZ_UNIT_TEST_HOOK(new UnitTest::WorldPartitionTestsEnvironment);
