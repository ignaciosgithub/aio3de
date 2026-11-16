/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzTest/AzTest.h>

#include <BehaviorTreePaths.h>

namespace BehaviorTree::Tests
{
    TEST(BehaviorTreePaths, RelativePathPrefersProjectSource)
    {
        const AZStd::vector<AZStd::string> candidates = GetTreePathCandidates("ai/guard.json");
        ASSERT_EQ(candidates.size(), 2);
        EXPECT_EQ(candidates[0], "@projectroot@/ai/guard.json");
        EXPECT_EQ(candidates[1], "@products@/ai/guard.json");
    }

    TEST(BehaviorTreePaths, MixedCasePathIncludesLowercaseAndPreservingProducts)
    {
        const AZStd::vector<AZStd::string> candidates = GetTreePathCandidates("Gems/AI/Guard.json");
        ASSERT_EQ(candidates.size(), 3);
        EXPECT_EQ(candidates[0], "@projectroot@/Gems/AI/Guard.json");
        EXPECT_EQ(candidates[1], "@products@/gems/ai/guard.json");
        EXPECT_EQ(candidates[2], "@products@/Gems/AI/Guard.json");
    }

    TEST(BehaviorTreePaths, LowercasePathDoesNotDuplicateProductCandidate)
    {
        const AZStd::vector<AZStd::string> candidates = GetTreePathCandidates("gems/ai/guard.json");
        ASSERT_EQ(candidates.size(), 2);
        EXPECT_EQ(candidates[0], "@projectroot@/gems/ai/guard.json");
        EXPECT_EQ(candidates[1], "@products@/gems/ai/guard.json");
    }

    TEST(BehaviorTreePaths, AbsolutePathsAreUnchanged)
    {
        const AZStd::vector<AZStd::string> unixPath = GetTreePathCandidates("/tmp/Guard.json");
        ASSERT_EQ(unixPath.size(), 1);
        EXPECT_EQ(unixPath[0], "/tmp/Guard.json");

        const AZStd::vector<AZStd::string> windowsPath = GetTreePathCandidates("C:\\Trees\\Guard.json");
        ASSERT_EQ(windowsPath.size(), 1);
        EXPECT_EQ(windowsPath[0], "C:\\Trees\\Guard.json");
    }

    TEST(BehaviorTreePaths, AliasedPathIsUnchanged)
    {
        const AZStd::vector<AZStd::string> candidates = GetTreePathCandidates("@products@/Gems/AI/Guard.json");
        ASSERT_EQ(candidates.size(), 1);
        EXPECT_EQ(candidates[0], "@products@/Gems/AI/Guard.json");
    }

    TEST(BehaviorTreePaths, BackslashesAndLeadingDotAreNormalized)
    {
        const AZStd::vector<AZStd::string> candidates =
            GetTreePathCandidates(".\\Gems\\AI\\Guard.json");
        ASSERT_EQ(candidates.size(), 3);
        EXPECT_EQ(candidates[0], "@projectroot@/Gems/AI/Guard.json");
        EXPECT_EQ(candidates[1], "@products@/gems/ai/guard.json");
        EXPECT_EQ(candidates[2], "@products@/Gems/AI/Guard.json");
    }

    TEST(BehaviorTreePaths, EmptyPathHasNoCandidates)
    {
        EXPECT_TRUE(GetTreePathCandidates("").empty());
    }
} // namespace BehaviorTree::Tests
