/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "BehaviorTreeNodes.h"

#include <AzCore/Math/MathUtils.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/limits.h>
#include <AzCore/std/string/conversions.h>
#include <AzCore/std/string/string_view.h>

#include <cstdlib>

namespace BehaviorTree
{
    namespace
    {
        const AZStd::string& StringParameter(const NodeDefinition& definition, const AZStd::string& name)
        {
            static const AZStd::string empty;
            const ParameterValue* value = definition.FindParameter(name);
            const AZStd::string* result = value ? AZStd::get_if<AZStd::string>(value) : nullptr;
            return result ? *result : empty;
        }

        AZ::s32 IntParameter(const NodeDefinition& definition, const AZStd::string& name, AZ::s32 fallback)
        {
            const ParameterValue* value = definition.FindParameter(name);
            const AZ::s32* result = value ? AZStd::get_if<AZ::s32>(value) : nullptr;
            return result ? *result : fallback;
        }

        float FloatParameter(const NodeDefinition& definition, const AZStd::string& name, float fallback)
        {
            const ParameterValue* value = definition.FindParameter(name);
            const float* result = value ? AZStd::get_if<float>(value) : nullptr;
            return result ? *result : fallback;
        }

        bool BoolParameter(const NodeDefinition& definition, const AZStd::string& name, bool fallback)
        {
            const ParameterValue* value = definition.FindParameter(name);
            const bool* result = value ? AZStd::get_if<bool>(value) : nullptr;
            return result ? *result : fallback;
        }

        AZ::u32 NextRandom(AZ::u32& state)
        {
            if (state == 0)
            {
                state = 0x9E3779B9u;
            }
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        }

        float ParseFloat(const AZStd::string& text, bool& success)
        {
            char* end = nullptr;
            const float value = strtof(text.c_str(), &end);
            success = end != text.c_str() && *end == '\0';
            return value;
        }

        AZ::u64 ParseEntityId(const AZStd::string& text, bool& success)
        {
            const char* begin = text.c_str();
            if (*begin == '[')
            {
                ++begin;
            }
            char* end = nullptr;
            const AZ::u64 value = strtoull(begin, &end, 10);
            if (end != begin && *end == ']')
            {
                ++end;
            }
            success = end != begin && *end == '\0';
            return value;
        }

        bool ParseVector3(const AZStd::string& text, AZ::Vector3& value)
        {
            AZStd::vector<float> numbers;
            const char* current = text.c_str();
            while (*current)
            {
                while (*current == ' ' || *current == ',' || *current == '[' || *current == ']')
                {
                    ++current;
                }
                if (!*current)
                {
                    break;
                }
                char* end = nullptr;
                const float component = strtof(current, &end);
                if (end == current)
                {
                    return false;
                }
                numbers.push_back(component);
                current = end;
                while (*current == ' ')
                {
                    ++current;
                }
                if (*current && *current != ',')
                {
                    return false;
                }
            }
            if (numbers.size() != 3)
            {
                return false;
            }
            value = AZ::Vector3(numbers[0], numbers[1], numbers[2]);
            return true;
        }

        bool ParseTypedValue(BlackboardType type, const AZStd::string& text, Blackboard::Value& outValue)
        {
            switch (type)
            {
            case BlackboardType::Bool:
                if (text == "true")
                {
                    outValue = true;
                    return true;
                }
                if (text == "false")
                {
                    outValue = false;
                    return true;
                }
                return false;
            case BlackboardType::Float:
            {
                bool success = false;
                const float value = ParseFloat(text, success);
                if (success)
                {
                    outValue = value;
                }
                return success;
            }
            case BlackboardType::String:
                outValue = text;
                return true;
            case BlackboardType::Vector3:
            {
                AZ::Vector3 value;
                if (ParseVector3(text, value))
                {
                    outValue = value;
                    return true;
                }
                return false;
            }
            case BlackboardType::EntityId:
            {
                bool success = false;
                const AZ::u64 value = ParseEntityId(text, success);
                if (success)
                {
                    outValue = AZ::EntityId(value);
                }
                return success;
            }
            }
            return false;
        }

        bool IsLess(const Blackboard::Value& lhs, const Blackboard::Value& rhs)
        {
            if (lhs.index() != rhs.index())
            {
                return false;
            }
            switch (lhs.index())
            {
            case 0:
                return AZStd::get<bool>(lhs) < AZStd::get<bool>(rhs);
            case 1:
                return AZStd::get<float>(lhs) < AZStd::get<float>(rhs);
            case 2:
                return AZStd::get<AZStd::string>(lhs) < AZStd::get<AZStd::string>(rhs);
            case 3:
            {
                const AZ::Vector3& a = AZStd::get<AZ::Vector3>(lhs);
                const AZ::Vector3& b = AZStd::get<AZ::Vector3>(rhs);
                if (a.GetX() != b.GetX())
                {
                    return a.GetX() < b.GetX();
                }
                if (a.GetY() != b.GetY())
                {
                    return a.GetY() < b.GetY();
                }
                return a.GetZ() < b.GetZ();
            }
            default:
                return static_cast<AZ::u64>(AZStd::get<AZ::EntityId>(lhs)) <
                    static_cast<AZ::u64>(AZStd::get<AZ::EntityId>(rhs));
            }
        }

        bool ValuesEqual(const Blackboard::Value& lhs, const Blackboard::Value& rhs)
        {
            if (lhs.index() != rhs.index())
            {
                return false;
            }
            switch (lhs.index())
            {
            case 0:
                return AZStd::get<bool>(lhs) == AZStd::get<bool>(rhs);
            case 1:
                return AZStd::get<float>(lhs) == AZStd::get<float>(rhs);
            case 2:
                return AZStd::get<AZStd::string>(lhs) == AZStd::get<AZStd::string>(rhs);
            case 3:
                return AZStd::get<AZ::Vector3>(lhs).IsClose(AZStd::get<AZ::Vector3>(rhs));
            default:
                return AZStd::get<AZ::EntityId>(lhs) == AZStd::get<AZ::EntityId>(rhs);
            }
        }

        CompareOp ParseCompareOp(const AZStd::string& operation)
        {
            if (operation == "Equal") return CompareOp::Equal;
            if (operation == "NotEqual") return CompareOp::NotEqual;
            if (operation == "Less") return CompareOp::Less;
            if (operation == "LessEqual") return CompareOp::LessEqual;
            if (operation == "Greater") return CompareOp::Greater;
            if (operation == "GreaterEqual") return CompareOp::GreaterEqual;
            if (operation == "IsTrue") return CompareOp::IsTrue;
            if (operation == "IsFalse") return CompareOp::IsFalse;
            return CompareOp::Exists;
        }

        bool CompareBlackboard(const Blackboard& blackboard, const AZStd::string& key, CompareOp op, const AZStd::string& text)
        {
            const Blackboard::Value* value = blackboard.Find(key);
            if (op == CompareOp::Exists)
            {
                return value != nullptr;
            }
            if (!value)
            {
                return false;
            }
            if (op == CompareOp::IsTrue || op == CompareOp::IsFalse)
            {
                const bool* boolValue = AZStd::get_if<bool>(value);
                return boolValue && (op == CompareOp::IsTrue ? *boolValue : !*boolValue);
            }

            Blackboard::Value compareValue = false;
            if (!ParseTypedValue(Blackboard::GetType(*value), text, compareValue))
            {
                return false;
            }
            const bool equal = ValuesEqual(*value, compareValue);
            switch (op)
            {
            case CompareOp::Equal:
                return equal;
            case CompareOp::NotEqual:
                return !equal;
            case CompareOp::Less:
                return IsLess(*value, compareValue);
            case CompareOp::LessEqual:
                return equal || IsLess(*value, compareValue);
            case CompareOp::Greater:
                return IsLess(compareValue, *value);
            case CompareOp::GreaterEqual:
                return equal || IsLess(compareValue, *value);
            case CompareOp::Exists:
            case CompareOp::IsTrue:
            case CompareOp::IsFalse:
                break;
            }
            return false;
        }

        BlackboardType ParseBlackboardType(const AZStd::string& typeName, bool& valid)
        {
            valid = true;
            if (typeName == "bool") return BlackboardType::Bool;
            if (typeName == "float") return BlackboardType::Float;
            if (typeName == "string") return BlackboardType::String;
            if (typeName == "vector3") return BlackboardType::Vector3;
            if (typeName == "entity") return BlackboardType::EntityId;
            valid = false;
            return BlackboardType::Float;
        }
    } // namespace

    RuntimeNode::RuntimeNode(const NodeDefinition& definition, AZStd::string path)
        : m_definition(definition)
        , m_path(AZStd::move(path))
    {
        m_children.reserve(m_definition.m_children.size());
        for (size_t index = 0; index < m_definition.m_children.size(); ++index)
        {
            const AZStd::string childPath = AZStd::string::format("%s/%zu", m_path.c_str(), index);
            m_children.push_back(AZStd::make_unique<RuntimeNode>(m_definition.m_children[index], childPath));
        }
        m_parallelStatuses.resize(m_children.size(), NodeStatus::Running);
    }

    AZStd::string RuntimeNode::GetString(const AZStd::string& key) const
    {
        return StringParameter(m_definition, key);
    }

    float RuntimeNode::GetFloat(const AZStd::string& key, float fallback) const
    {
        return FloatParameter(m_definition, key, fallback);
    }

    AZ::s32 RuntimeNode::GetInt(const AZStd::string& key, AZ::s32 fallback) const
    {
        return IntParameter(m_definition, key, fallback);
    }

    bool RuntimeNode::GetBool(const AZStd::string& key, bool fallback) const
    {
        return BoolParameter(m_definition, key, fallback);
    }

    NodeStatus RuntimeNode::Tick(NodeTick& tick)
    {
        const AZStd::string& type = m_definition.m_type;
        if (type == "Sequence")
        {
            m_lastStatus = TickSequence(tick, false, GetBool("reactive", false));
        }
        else if (type == "Selector")
        {
            m_lastStatus = TickSequence(tick, true, GetBool("reactive", false));
        }
        else if (type == "Parallel")
        {
            m_lastStatus = TickParallel(tick);
        }
        else if (type == "Inverter" || type == "AlwaysSucceed" || type == "Repeat" ||
            type == "Cooldown" || type == "Timeout")
        {
            m_lastStatus = TickDecorator(tick);
        }
        else
        {
            m_lastStatus = TickLeaf(tick);
        }
        return m_lastStatus;
    }

    NodeStatus RuntimeNode::TickSequence(NodeTick& tick, bool selector, bool reactive)
    {
        if (selector && m_definition.m_type != "Selector")
        {
            return NodeStatus::Failure;
        }
        if (m_children.empty())
        {
            return selector ? NodeStatus::Failure : NodeStatus::Success;
        }

        const size_t previousRunningChild = m_runningChild;
        const bool hadRunningChild = m_started;
        size_t startIndex = hadRunningChild ? previousRunningChild : 0;
        if (reactive && hadRunningChild)
        {
            for (size_t index = 0; index < previousRunningChild; ++index)
            {
                const NodeStatus status = m_children[index]->Tick(tick);
                const bool preempted = selector ? status != NodeStatus::Failure : status != NodeStatus::Success;
                if (preempted)
                {
                    m_children[previousRunningChild]->Abort(tick.m_context);
                    m_started = status == NodeStatus::Running;
                    m_runningChild = index;
                    return status;
                }
            }
            startIndex = previousRunningChild;
        }

        m_started = false;
        for (size_t index = startIndex; index < m_children.size(); ++index)
        {
            const NodeStatus status = m_children[index]->Tick(tick);
            if (selector)
            {
                if (status == NodeStatus::Success)
                {
                    m_runningChild = 0;
                    return NodeStatus::Success;
                }
                if (status == NodeStatus::Running)
                {
                    m_runningChild = index;
                    m_started = true;
                    return NodeStatus::Running;
                }
            }
            else
            {
                if (status == NodeStatus::Failure)
                {
                    m_runningChild = 0;
                    return NodeStatus::Failure;
                }
                if (status == NodeStatus::Running)
                {
                    m_runningChild = index;
                    m_started = true;
                    return NodeStatus::Running;
                }
            }
        }
        m_runningChild = 0;
        return selector ? NodeStatus::Failure : NodeStatus::Success;
    }

    NodeStatus RuntimeNode::TickParallel(NodeTick& tick)
    {
        AZ::s32 successes = 0;
        AZ::s32 failures = 0;
        const AZ::s32 successThreshold = GetInt("successThreshold", -1) < 0
            ? aznumeric_cast<AZ::s32>(m_children.size())
            : GetInt("successThreshold", -1);
        const AZ::s32 failureThreshold = AZStd::max(GetInt("failureThreshold", 1), 1);

        for (size_t index = 0; index < m_children.size(); ++index)
        {
            if (m_parallelStatuses[index] == NodeStatus::Running)
            {
                m_parallelStatuses[index] = m_children[index]->Tick(tick);
            }
            if (m_parallelStatuses[index] == NodeStatus::Success)
            {
                ++successes;
            }
            else if (m_parallelStatuses[index] == NodeStatus::Failure)
            {
                ++failures;
            }
        }

        NodeStatus result = NodeStatus::Running;
        if (successes >= successThreshold)
        {
            result = NodeStatus::Success;
        }
        else if (failures >= failureThreshold)
        {
            result = NodeStatus::Failure;
        }
        if (result != NodeStatus::Running)
        {
            AbortRunningChildren(tick.m_context);
            AZStd::fill(m_parallelStatuses.begin(), m_parallelStatuses.end(), NodeStatus::Running);
        }
        return result;
    }

    NodeStatus RuntimeNode::TickDecorator(NodeTick& tick)
    {
        RuntimeNode& child = *m_children.front();
        if (m_definition.m_type == "Inverter")
        {
            const NodeStatus status = child.Tick(tick);
            return status == NodeStatus::Success ? NodeStatus::Failure :
                status == NodeStatus::Failure ? NodeStatus::Success : NodeStatus::Running;
        }
        if (m_definition.m_type == "AlwaysSucceed")
        {
            return child.Tick(tick) == NodeStatus::Running ? NodeStatus::Running : NodeStatus::Success;
        }
        if (m_definition.m_type == "Repeat")
        {
            const AZ::s32 count = GetInt("count", -1);
            if (count == 0)
            {
                return NodeStatus::Success;
            }
            const NodeStatus status = child.Tick(tick);
            if (status == NodeStatus::Running)
            {
                return NodeStatus::Running;
            }
        if (status == NodeStatus::Failure)
        {
            return GetBool("untilFailure", false) ? NodeStatus::Success : NodeStatus::Failure;
        }
            if (count >= 0)
            {
                ++m_repeatCount;
            }
            if (count >= 0 && m_repeatCount >= count)
            {
                m_repeatCount = 0;
                return NodeStatus::Success;
            }
            child.Reset(tick.m_context);
            return NodeStatus::Running;
        }
        if (m_definition.m_type == "Cooldown")
        {
            if (tick.m_time < m_cooldownUntil)
            {
                return NodeStatus::Failure;
            }
            const NodeStatus status = child.Tick(tick);
            if (status != NodeStatus::Running)
            {
                m_cooldownUntil = tick.m_time + AZStd::max(GetFloat("seconds", 0.0f), 0.0f);
            }
            return status;
        }

        const float timeout = AZStd::max(GetFloat("seconds", 0.0f), 0.0f);
        if (!m_started)
        {
            m_started = true;
            m_startedAt = tick.m_time;
        }
        if (tick.m_time - m_startedAt >= timeout)
        {
            child.Abort(tick.m_context);
            m_started = false;
            return NodeStatus::Failure;
        }
        const NodeStatus status = child.Tick(tick);
        if (status != NodeStatus::Running)
        {
            m_started = false;
        }
        return status;
    }

    NodeStatus RuntimeNode::TickLeaf(NodeTick& tick)
    {
        if (m_definition.m_type == "Wait")
        {
            if (!m_started)
            {
                const float deviation = AZStd::max(GetFloat("randomDeviation", 0.0f), 0.0f);
                const float randomUnit = static_cast<float>(NextRandom(tick.m_randomState)) /
                    static_cast<float>(AZStd::numeric_limits<AZ::u32>::max());
                m_elapsed = AZStd::max(0.0f, GetFloat("seconds", 1.0f) + (2.0f * randomUnit - 1.0f) * deviation);
                m_started = true;
                m_startedAt = 0.0f;
            }
            m_startedAt += tick.m_deltaTime;
            if (m_startedAt >= m_elapsed)
            {
                m_started = false;
                return NodeStatus::Success;
            }
            return NodeStatus::Running;
        }
        if (m_definition.m_type == "CheckBlackboard")
        {
            return CompareBlackboard(
                tick.m_blackboard, GetString("key"), ParseCompareOp(GetString("op")), GetString("value"))
                ? NodeStatus::Success
                : NodeStatus::Failure;
        }
        if (m_definition.m_type == "SetBlackboard")
        {
            const AZStd::string key = GetString("key");
            bool validType = false;
            const BlackboardType type = ParseBlackboardType(GetString("valueType"), validType);
            Blackboard::Value value = false;
            if (!validType || !ParseTypedValue(type, GetString("value"), value))
            {
                return NodeStatus::Failure;
            }
            tick.m_blackboard.Set(key, AZStd::move(value));
            return NodeStatus::Success;
        }
        if (m_definition.m_type == "Action")
        {
            const int rawStatus = tick.m_context.TickAction(GetString("name"), tick.m_deltaTime);
            if (rawStatus < static_cast<int>(NodeStatus::Success) || rawStatus > static_cast<int>(NodeStatus::Running))
            {
                return NodeStatus::Failure;
            }
            return static_cast<NodeStatus>(rawStatus);
        }
        if (m_definition.m_type == "Condition")
        {
            return tick.m_context.EvaluateCondition(GetString("name")) ? NodeStatus::Success : NodeStatus::Failure;
        }
        if (m_definition.m_type == "Log")
        {
            tick.m_context.Log(GetString("message"));
            return NodeStatus::Success;
        }
        if (m_definition.m_type == "MoveTo")
        {
            const AZStd::string targetKey = GetString("targetKey");
            AZ::Vector3 target;
            const Blackboard::Value* value = tick.m_blackboard.Find(targetKey);
            if (value)
            {
                if (const AZ::Vector3* vectorValue = AZStd::get_if<AZ::Vector3>(value))
                {
                    target = *vectorValue;
                }
                else if (const AZ::EntityId* entityValue = AZStd::get_if<AZ::EntityId>(value))
                {
                    if (!tick.m_context.GetEntityPosition(*entityValue, target))
                    {
                        return NodeStatus::Failure;
                    }
                }
                else
                {
                    return NodeStatus::Failure;
                }
            }
            else
            {
                return NodeStatus::Failure;
            }

            const AZ::Vector3 position = tick.m_context.GetPosition();
            const float acceptanceRadius = AZStd::max(GetFloat("acceptanceRadius", 0.5f), 0.0f);
            const float speed = AZStd::max(GetFloat("speed", 4.0f), 0.0f);
            if (position.GetDistance(target) <= acceptanceRadius)
            {
                tick.m_context.StopMoving();
                m_hasPath = false;
                return NodeStatus::Success;
            }

            if (!m_hasPath || tick.m_time >= m_nextRepathTime)
            {
                m_movePath = tick.m_context.FindPath(position, target);
                m_waypointIndex = 0;
                m_hasPath = !m_movePath.empty();
                m_nextRepathTime = tick.m_time + AZStd::max(GetFloat("repathInterval", 0.5f), 0.0f);
                if (!m_hasPath)
                {
                    tick.m_context.StopMoving();
                    return NodeStatus::Failure;
                }
            }
            const float advanceRadius = AZStd::max(0.25f, speed * tick.m_deltaTime);
            while (m_waypointIndex + 1 < m_movePath.size() &&
                position.GetDistance(m_movePath[m_waypointIndex]) <= advanceRadius)
            {
                ++m_waypointIndex;
            }
            const AZ::Vector3 direction = m_movePath[m_waypointIndex] - position;
            if (!direction.IsZero())
            {
                tick.m_context.MoveToward(direction.GetNormalized(), speed);
            }
            return NodeStatus::Running;
        }
        return NodeStatus::Failure;
    }

    void RuntimeNode::AbortRunningChildren(TreeContext& context)
    {
        for (AZStd::unique_ptr<RuntimeNode>& child : m_children)
        {
            if (child->m_lastStatus == NodeStatus::Running)
            {
                child->Abort(context);
            }
        }
    }

    void RuntimeNode::Abort(TreeContext& context)
    {
        if (m_lastStatus == NodeStatus::Running)
        {
            if (m_definition.m_type == "Action")
            {
                context.AbortAction(GetString("name"));
            }
            else if (m_definition.m_type == "MoveTo")
            {
                context.StopMoving();
            }
        }
        AbortRunningChildren(context);
        m_lastStatus = NodeStatus::Failure;
        m_runningChild = 0;
        m_repeatCount = 0;
        m_elapsed = 0.0f;
        m_startedAt = 0.0f;
        m_cooldownUntil = 0.0f;
        m_nextRepathTime = 0.0f;
        m_started = false;
        m_hasPath = false;
        m_movePath.clear();
        m_waypointIndex = 0;
        AZStd::fill(m_parallelStatuses.begin(), m_parallelStatuses.end(), NodeStatus::Running);
    }

    void RuntimeNode::Reset(TreeContext& context)
    {
        if (m_lastStatus == NodeStatus::Running)
        {
            Abort(context);
        }
        else
        {
            m_lastStatus = NodeStatus::Failure;
            m_runningChild = 0;
            m_repeatCount = 0;
            m_elapsed = 0.0f;
            m_startedAt = 0.0f;
            m_nextRepathTime = 0.0f;
            m_started = false;
            m_hasPath = false;
            m_movePath.clear();
            m_waypointIndex = 0;
            AZStd::fill(m_parallelStatuses.begin(), m_parallelStatuses.end(), NodeStatus::Running);
        }
        for (AZStd::unique_ptr<RuntimeNode>& child : m_children)
        {
            child->Reset(context);
        }
    }

    void RuntimeNode::CollectActiveNodePaths(AZStd::vector<AZStd::string>& paths) const
    {
        if (m_lastStatus == NodeStatus::Running)
        {
            const AZStd::string displayName = m_definition.m_name.empty() ?
                StringParameter(m_definition, "name") : m_definition.m_name;
            paths.push_back(displayName.empty() ? m_path : AZStd::string::format("%s %s", m_path.c_str(), displayName.c_str()));
        }
        for (const AZStd::unique_ptr<RuntimeNode>& child : m_children)
        {
            child->CollectActiveNodePaths(paths);
        }
    }
} // namespace BehaviorTree
