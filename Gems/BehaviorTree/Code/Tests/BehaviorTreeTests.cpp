/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/UnitTest/TestTypes.h>
#include <AzTest/AzTest.h>

#include <BehaviorTreeParser.h>
#include <BehaviorTreeWriter.h>
#include <NodeRegistry.h>
#include <TreeInstance.h>

#include <filesystem>
#include <fstream>
#include <iterator>

namespace BehaviorTree::Tests
{
    class MockContext final : public TreeContext
    {
    public:
        AZ::Vector3 GetPosition() const override { return m_position; }
        AZStd::vector<AZ::Vector3> FindPath(const AZ::Vector3& from, const AZ::Vector3& to) override
        {
            ++m_pathCalls;
            if (m_emptyPath)
            {
                return {};
            }
            return m_customPath.empty() ? AZStd::vector<AZ::Vector3>{ from, to } : m_customPath;
        }
        void MoveToward(const AZ::Vector3& direction, float speed) override
        {
            m_lastDirection = direction;
            m_lastSpeed = speed;
            ++m_moveCalls;
            m_position += direction * speed * m_moveDeltaTime;
        }
        void StopMoving() override { ++m_stopCalls; }
        bool GetEntityPosition(const AZ::EntityId& entityId, AZ::Vector3& outPosition) const override
        {
            auto it = m_entityPositions.find(entityId);
            if (it == m_entityPositions.end()) return false;
            outPosition = it->second;
            return true;
        }
        int TickAction(const AZStd::string& action, float) override
        {
            ++m_actionTicks[action];
            auto it = m_actionResults.find(action);
            if (it == m_actionResults.end() || it->second.empty()) return static_cast<int>(NodeStatus::Success);
            AZStd::vector<int>& results = it->second;
            const size_t index = AZStd::min(m_actionTickIndices[action]++, results.size() - 1);
            return results[index];
        }
        void AbortAction(const AZStd::string& action) override { ++m_actionAborts[action]; }
        bool EvaluateCondition(const AZStd::string& condition) override
        {
            ++m_conditionTicks[condition];
            auto it = m_conditionResults.find(condition);
            if (it == m_conditionResults.end() || it->second.empty()) return false;
            AZStd::vector<bool>& results = it->second;
            const size_t index = AZStd::min(m_conditionTickIndices[condition]++, results.size() - 1);
            return results[index];
        }
        void Log(const AZStd::string& message) override { m_logs.push_back(message); }

        AZ::Vector3 m_position = AZ::Vector3::CreateZero();
        AZStd::vector<AZ::Vector3> m_customPath;
        AZStd::unordered_map<AZStd::string, AZStd::vector<int>> m_actionResults;
        AZStd::unordered_map<AZStd::string, AZStd::vector<bool>> m_conditionResults;
        AZStd::unordered_map<AZ::EntityId, AZ::Vector3> m_entityPositions;
        AZStd::unordered_map<AZStd::string, size_t> m_actionTickIndices;
        AZStd::unordered_map<AZStd::string, size_t> m_conditionTickIndices;
        AZStd::unordered_map<AZStd::string, int> m_actionTicks;
        AZStd::unordered_map<AZStd::string, int> m_actionAborts;
        AZStd::unordered_map<AZStd::string, int> m_conditionTicks;
        AZStd::vector<AZStd::string> m_logs;
        AZ::Vector3 m_lastDirection = AZ::Vector3::CreateZero();
        float m_lastSpeed = 0.0f;
        float m_moveDeltaTime = 0.0f;
        int m_pathCalls = 0;
        int m_moveCalls = 0;
        int m_stopCalls = 0;
        bool m_emptyPath = false;
    };

    BehaviorTreeDefinition ParseTree(const char* json)
    {
        auto result = BehaviorTreeParser::Parse(json);
        AZ_Assert(result.IsSuccess(), "%s", result.IsSuccess() ? "" : result.GetError().c_str());
        return result.TakeValue();
    }

    TEST(BehaviorTreeParser, ValidDefinitionDefaultsAndWriterRoundTrip)
    {
        const char* json = R"({"version":1,"blackboard":{"alert":{"type":"bool","value":false}},"root":{"type":"Sequence","children":[{"type":"Wait"}]}})";
        auto parsed = BehaviorTreeParser::Parse(json);
        ASSERT_TRUE(parsed.IsSuccess()) << parsed.GetError().c_str();
        const BehaviorTreeDefinition definition = parsed.GetValue();
        EXPECT_EQ(AZStd::get<float>(*definition.m_root.m_children[0].FindParameter("seconds")), 1.0f);
        EXPECT_EQ(definition.m_blackboard.at("alert").m_value.index(), 0);
        const AZStd::string encoded = BehaviorTreeWriter::Write(definition);
        auto reparsed = BehaviorTreeParser::Parse(encoded);
        ASSERT_TRUE(reparsed.IsSuccess()) << reparsed.GetError().c_str();
        EXPECT_EQ(BehaviorTreeWriter::Write(reparsed.GetValue()), encoded);
    }

    TEST(BehaviorTreeParser, GuardExampleFileParses)
    {
        const std::filesystem::path path =
            std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
            "Examples/Guard.behaviortree.json";
        std::ifstream file(path);
        ASSERT_TRUE(file.is_open()) << path.string();
        const std::string json{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
        const auto result = BehaviorTreeParser::Parse(AZStd::string_view(json.c_str(), json.size()));
        ASSERT_TRUE(result.IsSuccess()) << result.GetError().c_str();
    }

    TEST(BehaviorTreeParser, ReportsUnknownTypeAndBadChildCountWithPaths)
    {
        auto unknown = BehaviorTreeParser::Parse(R"({"root":{"type":"Sequence","children":[{"type":"Foo"}]}})");
        ASSERT_FALSE(unknown.IsSuccess());
        EXPECT_NE(unknown.GetError().find("root/children[0]"), AZStd::string::npos);
        auto badCount = BehaviorTreeParser::Parse(R"({"root":{"type":"Inverter","children":[]}})");
        ASSERT_FALSE(badCount.IsSuccess());
        EXPECT_NE(badCount.GetError().find("root"), AZStd::string::npos);
    }

    TEST(BehaviorTreeParser, RejectsUnknownParametersAndWrongTypesWithPaths)
    {
        auto unknown = BehaviorTreeParser::Parse(R"({"root":{"type":"Wait","typo":1}})");
        ASSERT_FALSE(unknown.IsSuccess());
        EXPECT_NE(unknown.GetError().find("unknown parameter"), AZStd::string::npos);
        auto wrongType = BehaviorTreeParser::Parse(R"({"root":{"type":"Wait","seconds":"one"}})");
        ASSERT_FALSE(wrongType.IsSuccess());
        EXPECT_NE(wrongType.GetError().find("root"), AZStd::string::npos);
    }

    TEST(BehaviorTreeRuntime, SequenceAndSelectorResumeRunningChildWithoutRetickingEarlierChildren)
    {
        MockContext context;
        context.m_actionResults["run"] = { static_cast<int>(NodeStatus::Running), static_cast<int>(NodeStatus::Success) };
        TreeInstance sequence(ParseTree(
            R"({"root":{"type":"Sequence","children":[{"type":"Action","name":"first"},{"type":"Action","name":"run"}]}})"));
        EXPECT_EQ(sequence.Tick(0.1f, context), NodeStatus::Running);
        EXPECT_EQ(sequence.Tick(0.1f, context), NodeStatus::Success);
        EXPECT_EQ(context.m_actionTicks["first"], 1);
        EXPECT_EQ(context.m_actionTicks["run"], 2);

        context = {};
        context.m_actionResults["first"] = { static_cast<int>(NodeStatus::Failure) };
        context.m_actionResults["run"] = { static_cast<int>(NodeStatus::Running), static_cast<int>(NodeStatus::Success) };
        TreeInstance selector(ParseTree(
            R"({"root":{"type":"Selector","children":[{"type":"Action","name":"first"},{"type":"Action","name":"run"}]}})"));
        EXPECT_EQ(selector.Tick(0.1f, context), NodeStatus::Running);
        EXPECT_EQ(selector.Tick(0.1f, context), NodeStatus::Success);
        EXPECT_EQ(context.m_actionTicks["first"], 1);
    }

    TEST(BehaviorTreeRuntime, ReactiveSelectorAndSequenceAbortPreemptedRunningChildrenOnce)
    {
        MockContext context;
        context.m_conditionResults["high"] = { false, true };
        context.m_actionResults["lower"] = { static_cast<int>(NodeStatus::Running) };
        TreeInstance selector(ParseTree(
            R"({"root":{"type":"Selector","reactive":true,"children":[{"type":"Condition","name":"high"},{"type":"Action","name":"lower"}]}})"));
        EXPECT_EQ(selector.Tick(0.1f, context), NodeStatus::Running);
        EXPECT_EQ(selector.Tick(0.1f, context), NodeStatus::Success);
        EXPECT_EQ(context.m_actionAborts["lower"], 1);

        context = {};
        context.m_conditionResults["guard"] = { true, false };
        context.m_actionResults["lower"] = { static_cast<int>(NodeStatus::Running) };
        TreeInstance sequence(ParseTree(
            R"({"root":{"type":"Sequence","reactive":true,"children":[{"type":"Condition","name":"guard"},{"type":"Action","name":"lower"}]}})"));
        EXPECT_EQ(sequence.Tick(0.1f, context), NodeStatus::Running);
        EXPECT_EQ(sequence.Tick(0.1f, context), NodeStatus::Failure);
        EXPECT_EQ(context.m_actionAborts["lower"], 1);
    }

    TEST(BehaviorTreeRuntime, ParallelThresholdAbortsRunningChildren)
    {
        MockContext context;
        context.m_actionResults["done"] = { static_cast<int>(NodeStatus::Success) };
        context.m_actionResults["running"] = { static_cast<int>(NodeStatus::Running) };
        TreeInstance tree(ParseTree(
            R"({"root":{"type":"Parallel","successThreshold":1,"children":[{"type":"Action","name":"done"},{"type":"Action","name":"running"}]}})"));
        EXPECT_EQ(tree.Tick(0.1f, context), NodeStatus::Success);
        EXPECT_EQ(context.m_actionAborts["running"], 1);

        context = {};
        context.m_actionResults["fail"] = { static_cast<int>(NodeStatus::Failure) };
        context.m_actionResults["running"] = { static_cast<int>(NodeStatus::Running) };
        TreeInstance failureTree(ParseTree(
            R"({"root":{"type":"Parallel","failureThreshold":1,"children":[{"type":"Action","name":"fail"},{"type":"Action","name":"running"}]}})"));
        EXPECT_EQ(failureTree.Tick(0.1f, context), NodeStatus::Failure);
        EXPECT_EQ(context.m_actionAborts["running"], 1);
    }

    TEST(BehaviorTreeRuntime, InverterAlwaysSucceedAndRepeatSemantics)
    {
        MockContext context;
        context.m_actionResults["fail"] = { static_cast<int>(NodeStatus::Failure) };
        TreeInstance inverter(ParseTree(
            R"({"root":{"type":"Inverter","children":[{"type":"Action","name":"fail"}]}})"));
        EXPECT_EQ(inverter.Tick(0.1f, context), NodeStatus::Success);
        TreeInstance always(ParseTree(
            R"({"root":{"type":"AlwaysSucceed","children":[{"type":"Action","name":"fail"}]}})"));
        EXPECT_EQ(always.Tick(0.1f, context), NodeStatus::Success);

        context = {};
        TreeInstance repeat(ParseTree(
            R"({"root":{"type":"Repeat","count":2,"children":[{"type":"Action","name":"run"}]}})"));
        EXPECT_EQ(repeat.Tick(0.1f, context), NodeStatus::Running);
        EXPECT_EQ(repeat.Tick(0.1f, context), NodeStatus::Success);
        EXPECT_EQ(context.m_actionTicks["run"], 2);
        context = {};
        context.m_actionResults["fail"] = { static_cast<int>(NodeStatus::Failure) };
        TreeInstance untilFailure(ParseTree(
            R"({"root":{"type":"Repeat","untilFailure":true,"children":[{"type":"Action","name":"fail"}]}})"));
        EXPECT_EQ(untilFailure.Tick(0.1f, context), NodeStatus::Success);
    }

    TEST(BehaviorTreeRuntime, CooldownAndTimeoutUseElapsedTimeAndAbortChild)
    {
        MockContext context;
        TreeInstance cooldown(ParseTree(
            R"({"root":{"type":"Cooldown","seconds":1.0,"children":[{"type":"Action","name":"done"}]}})"));
        EXPECT_EQ(cooldown.Tick(0.1f, context), NodeStatus::Success);
        EXPECT_EQ(cooldown.Tick(0.5f, context), NodeStatus::Failure);
        EXPECT_EQ(context.m_actionTicks["done"], 1);
        EXPECT_EQ(cooldown.Tick(0.6f, context), NodeStatus::Success);

        context = {};
        context.m_actionResults["long"] = { static_cast<int>(NodeStatus::Running) };
        TreeInstance timeout(ParseTree(
            R"({"root":{"type":"Timeout","seconds":1.0,"children":[{"type":"Action","name":"long"}]}})"));
        EXPECT_EQ(timeout.Tick(0.5f, context), NodeStatus::Running);
        EXPECT_EQ(timeout.Tick(1.1f, context), NodeStatus::Failure);
        EXPECT_EQ(context.m_actionAborts["long"], 1);
    }

    TEST(BehaviorTreeRuntime, WaitIsDeterministicForEqualSeeds)
    {
        const BehaviorTreeDefinition definition = ParseTree(R"({"root":{"type":"Wait","seconds":1.0,"randomDeviation":0.25}})");
        TreeInstance first(definition, 123u);
        TreeInstance second(definition, 123u);
        MockContext firstContext;
        MockContext secondContext;
        for (int tick = 0; tick < 5; ++tick)
        {
            EXPECT_EQ(first.Tick(0.2f, firstContext), second.Tick(0.2f, secondContext));
        }
    }

    TEST(BehaviorTreeRuntime, CheckBlackboardSupportsTypedComparisonsAndSetBlackboard)
    {
        const char* blackboard = R"("number":{"type":"float","value":2.0},"enabled":{"type":"bool","value":true},"disabled":{"type":"bool","value":false},"label":{"type":"string","value":"guard"},"point":{"type":"vector3","value":[1,2,3]},"target":{"type":"entity","value":7})";
        const AZStd::string json = AZStd::string::format(
            R"({"blackboard":{%s},"root":{"type":"CheckBlackboard","key":"number","op":"GreaterEqual","value":"2"}})", blackboard);
        TreeInstance tree(ParseTree(json.c_str()));
        MockContext context;
        EXPECT_EQ(tree.Tick(0.1f, context), NodeStatus::Success);

        auto check = [&](const char* key, const char* op, const char* value)
        {
            BehaviorTreeDefinition definition = ParseTree(AZStd::string::format(
                R"({"blackboard":{%s},"root":{"type":"CheckBlackboard","key":"%s","op":"%s","value":"%s"}})",
                blackboard, key, op, value).c_str());
            TreeInstance instance(AZStd::move(definition));
            return instance.Tick(0.1f, context);
        };

        struct ComparisonCase
        {
            const char* m_key;
            const char* m_operation;
            const char* m_value;
            NodeStatus m_expected;
        };
        const ComparisonCase comparisons[] = {
            { "number", "Exists", "", NodeStatus::Success },
            { "missing", "Exists", "", NodeStatus::Failure },
            { "number", "Equal", "2", NodeStatus::Success },
            { "number", "NotEqual", "3", NodeStatus::Success },
            { "number", "Less", "3", NodeStatus::Success },
            { "number", "LessEqual", "2", NodeStatus::Success },
            { "number", "Greater", "1", NodeStatus::Success },
            { "number", "GreaterEqual", "2", NodeStatus::Success },
            { "enabled", "IsTrue", "", NodeStatus::Success },
            { "enabled", "IsFalse", "", NodeStatus::Failure },
            { "enabled", "Equal", "true", NodeStatus::Success },
            { "enabled", "NotEqual", "false", NodeStatus::Success },
            { "enabled", "Less", "false", NodeStatus::Failure },
            { "enabled", "LessEqual", "true", NodeStatus::Success },
            { "enabled", "Greater", "false", NodeStatus::Success },
            { "enabled", "GreaterEqual", "true", NodeStatus::Success },
            { "disabled", "IsFalse", "", NodeStatus::Success },
            { "disabled", "IsTrue", "", NodeStatus::Failure },
            { "disabled", "Equal", "false", NodeStatus::Success },
            { "disabled", "NotEqual", "true", NodeStatus::Success },
            { "disabled", "Less", "true", NodeStatus::Success },
            { "disabled", "LessEqual", "true", NodeStatus::Success },
            { "disabled", "Greater", "true", NodeStatus::Failure },
            { "disabled", "GreaterEqual", "false", NodeStatus::Success },
            { "label", "Equal", "guard", NodeStatus::Success },
            { "label", "NotEqual", "guarded", NodeStatus::Success },
            { "label", "Less", "z", NodeStatus::Success },
            { "label", "LessEqual", "guard", NodeStatus::Success },
            { "label", "Greater", "aardvark", NodeStatus::Success },
            { "label", "GreaterEqual", "guard", NodeStatus::Success },
            { "point", "Equal", "1,2,3", NodeStatus::Success },
            { "point", "NotEqual", "1,2,4", NodeStatus::Success },
            { "point", "Less", "1,2,4", NodeStatus::Success },
            { "point", "LessEqual", "1,2,3", NodeStatus::Success },
            { "point", "Greater", "1,2,2", NodeStatus::Success },
            { "point", "GreaterEqual", "1,2,3", NodeStatus::Success },
            { "target", "Equal", "7", NodeStatus::Success },
            { "target", "NotEqual", "8", NodeStatus::Success },
            { "target", "Less", "8", NodeStatus::Success },
            { "target", "LessEqual", "7", NodeStatus::Success },
            { "target", "Greater", "6", NodeStatus::Success },
            { "target", "GreaterEqual", "7", NodeStatus::Success },
        };
        for (const ComparisonCase& comparison : comparisons)
        {
            EXPECT_EQ(check(comparison.m_key, comparison.m_operation, comparison.m_value), comparison.m_expected)
                << comparison.m_key << " " << comparison.m_operation << " " << comparison.m_value;
        }

        TreeInstance setTree(ParseTree(
            R"({"root":{"type":"Sequence","children":[{"type":"SetBlackboard","key":"ready","valueType":"bool","value":"true"},{"type":"SetBlackboard","key":"number","valueType":"float","value":"2.5"},{"type":"SetBlackboard","key":"label","valueType":"string","value":"guard"},{"type":"SetBlackboard","key":"point","valueType":"vector3","value":"1,2,3"},{"type":"SetBlackboard","key":"target","valueType":"entity","value":"7"}]}})"));
        EXPECT_EQ(setTree.Tick(0.1f, context), NodeStatus::Success);
        EXPECT_TRUE(setTree.GetBlackboard().GetBool("ready"));
        EXPECT_FLOAT_EQ(setTree.GetBlackboard().GetFloat("number"), 2.5f);
        EXPECT_EQ(setTree.GetBlackboard().GetString("label"), "guard");
        EXPECT_TRUE(setTree.GetBlackboard().GetVector3("point").IsClose(AZ::Vector3(1.0f, 2.0f, 3.0f)));
        EXPECT_EQ(setTree.GetBlackboard().GetEntityId("target"), AZ::EntityId(7));
    }

    TEST(BehaviorTreeRuntime, MoveToUsesPathAndStopsOnArrivalOrAbort)
    {
        MockContext context;
        TreeInstance tree(ParseTree(
            R"({"blackboard":{"target":{"type":"vector3","value":[2,0,0]}},"root":{"type":"MoveTo","targetKey":"target","speed":3.0,"acceptanceRadius":0.25}})"));
        EXPECT_EQ(tree.Tick(0.1f, context), NodeStatus::Running);
        EXPECT_EQ(context.m_pathCalls, 1);
        EXPECT_EQ(context.m_moveCalls, 1);
        EXPECT_TRUE(context.m_lastDirection.IsClose(AZ::Vector3::CreateAxisX()));
        context.m_position = AZ::Vector3(2.0f, 0.0f, 0.0f);
        EXPECT_EQ(tree.Tick(0.1f, context), NodeStatus::Success);
        EXPECT_EQ(context.m_stopCalls, 1);

        context = {};
        context.m_emptyPath = true;
        TreeInstance noPath(ParseTree(
            R"({"blackboard":{"target":{"type":"vector3","value":[2,0,0]}},"root":{"type":"MoveTo","targetKey":"target"}})"));
        EXPECT_EQ(noPath.Tick(0.1f, context), NodeStatus::Failure);
        EXPECT_EQ(context.m_stopCalls, 1);

        context = {};
        TreeInstance repath(ParseTree(
            R"({"blackboard":{"target":{"type":"vector3","value":[2,0,0]}},"root":{"type":"MoveTo","targetKey":"target","repathInterval":0.5}})"));
        EXPECT_EQ(repath.Tick(0.1f, context), NodeStatus::Running);
        EXPECT_EQ(context.m_pathCalls, 1);
        EXPECT_EQ(repath.Tick(0.2f, context), NodeStatus::Running);
        EXPECT_EQ(context.m_pathCalls, 1);
        EXPECT_EQ(repath.Tick(0.3f, context), NodeStatus::Running);
        EXPECT_EQ(context.m_pathCalls, 2);

        context = {};
        context.m_conditionResults["guard"] = { true, false };
        TreeInstance abortedMove(ParseTree(
            R"({"blackboard":{"target":{"type":"vector3","value":[2,0,0]}},"root":{"type":"Sequence","reactive":true,"children":[{"type":"Condition","name":"guard"},{"type":"MoveTo","targetKey":"target"}]}})"));
        EXPECT_EQ(abortedMove.Tick(0.1f, context), NodeStatus::Running);
        EXPECT_EQ(abortedMove.Tick(0.1f, context), NodeStatus::Failure);
        EXPECT_EQ(context.m_stopCalls, 1);

        context = {};
        context.m_customPath = {
            AZ::Vector3::CreateZero(), AZ::Vector3(1.0f, 0.0f, 0.0f), AZ::Vector3(4.0f, 0.0f, 0.0f) };
        context.m_moveDeltaTime = 0.5f;
        TreeInstance fastMove(ParseTree(
            R"({"blackboard":{"target":{"type":"vector3","value":[4,0,0]}},"root":{"type":"MoveTo","targetKey":"target","speed":4.0,"acceptanceRadius":0.25,"repathInterval":100.0}})"));
        NodeStatus status = NodeStatus::Running;
        int tickCount = 0;
        for (; tickCount < 3 && status != NodeStatus::Success; ++tickCount)
        {
            status = fastMove.Tick(0.5f, context);
            if (status == NodeStatus::Running)
            {
                EXPECT_GT(context.m_lastDirection.GetX(), 0.0f);
            }
        }
        EXPECT_EQ(status, NodeStatus::Success);
        EXPECT_LE(tickCount, 3);
        EXPECT_TRUE(context.m_position.IsClose(AZ::Vector3(4.0f, 0.0f, 0.0f)));
    }

    TEST(BehaviorTreeRuntime, LoopAndActiveNodePaths)
    {
        MockContext context;
        context.m_actionResults["work"] = { static_cast<int>(NodeStatus::Running) };
        TreeInstance running(ParseTree(
            R"({"root":{"type":"Sequence","name":"Root","children":[{"type":"Action","name":"work"}]}})"));
        EXPECT_EQ(running.Tick(0.1f, context), NodeStatus::Running);
        const AZStd::vector<AZStd::string> paths = running.GetActiveNodePaths();
        EXPECT_EQ(paths.size(), 2);

        TreeInstance loop(ParseTree(R"({"root":{"type":"Log","message":"done"}})"));
        EXPECT_EQ(loop.Tick(0.1f, context), NodeStatus::Success);
        EXPECT_TRUE(loop.IsRunning());
        EXPECT_EQ(loop.Tick(0.1f, context), NodeStatus::Success);
        TreeInstance once(ParseTree(R"({"loop":false,"root":{"type":"Log","message":"done"}})"));
        EXPECT_EQ(once.Tick(0.1f, context), NodeStatus::Success);
        EXPECT_TRUE(once.IsComplete());
    }
} // namespace BehaviorTree::Tests

AZ_UNIT_TEST_HOOK(DEFAULT_UNIT_TEST_ENV);
