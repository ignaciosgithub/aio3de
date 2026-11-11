/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "NodeRegistry.h"

namespace BehaviorTree
{
    namespace
    {
        NodeParameterSchema Parameter(
            AZStd::string name,
            ParameterKind kind,
            ParameterValue defaultValue,
            AZStd::string description = {},
            AZStd::vector<AZStd::string> enumValues = {})
        {
            return { AZStd::move(name), kind, AZStd::move(defaultValue), AZStd::move(enumValues), AZStd::move(description) };
        }

        const AZStd::vector<NodeSchema>& Schemas()
        {
            static const AZStd::vector<NodeSchema> schemas = {
                { "Sequence", NodeCategory::Composite, ChildCountRule::AtLeastOne,
                    { Parameter("reactive", ParameterKind::Bool, false, "Re-evaluate earlier children while one child is running") },
                    "Succeeds when every child succeeds" },
                { "Selector", NodeCategory::Composite, ChildCountRule::AtLeastOne,
                    { Parameter("reactive", ParameterKind::Bool, false, "Re-evaluate earlier children while one child is running") },
                    "Succeeds when the first child succeeds" },
                { "Parallel", NodeCategory::Composite, ChildCountRule::AtLeastOne,
                    { Parameter("successThreshold", ParameterKind::Int, AZ::s32(-1), "Successes required; -1 means all"),
                      Parameter("failureThreshold", ParameterKind::Int, AZ::s32(1), "Failures required") },
                    "Ticks all unfinished children" },
                { "Inverter", NodeCategory::Decorator, ChildCountRule::ExactlyOne, {}, "Inverts Success and Failure" },
                { "AlwaysSucceed", NodeCategory::Decorator, ChildCountRule::ExactlyOne, {}, "Returns Success when its child finishes" },
                { "Repeat", NodeCategory::Decorator, ChildCountRule::ExactlyOne,
                    { Parameter("count", ParameterKind::Int, AZ::s32(-1), "Number of successful child runs; -1 repeats indefinitely"),
                      Parameter("untilFailure", ParameterKind::Bool, false, "Stop when the child fails") },
                    "Repeats its child" },
                { "Cooldown", NodeCategory::Decorator, ChildCountRule::ExactlyOne,
                    { Parameter("seconds", ParameterKind::Float, 0.0f, "Cooldown duration after the child finishes") },
                    "Fails without ticking the child while cooling down" },
                { "Timeout", NodeCategory::Decorator, ChildCountRule::ExactlyOne,
                    { Parameter("seconds", ParameterKind::Float, 0.0f, "Maximum time the child may run") },
                    "Aborts a child that exceeds the time limit" },
                { "Wait", NodeCategory::Leaf, ChildCountRule::None,
                    { Parameter("seconds", ParameterKind::Float, 1.0f, "Wait duration"),
                      Parameter("randomDeviation", ParameterKind::Float, 0.0f, "Deterministic random duration deviation") },
                    "Runs until its timer expires" },
                { "CheckBlackboard", NodeCategory::Leaf, ChildCountRule::None,
                    { Parameter("key", ParameterKind::Key, AZStd::string{}, "Blackboard key"),
                      Parameter("op", ParameterKind::Enum, AZStd::string("Exists"), "Comparison operation",
                          { "Exists", "Equal", "NotEqual", "Less", "LessEqual", "Greater", "GreaterEqual", "IsTrue", "IsFalse" }),
                      Parameter("value", ParameterKind::String, AZStd::string{}, "Comparison value parsed using the key type") },
                    "Tests a blackboard value" },
                { "SetBlackboard", NodeCategory::Leaf, ChildCountRule::None,
                    { Parameter("key", ParameterKind::Key, AZStd::string{}, "Blackboard key"),
                      Parameter("valueType", ParameterKind::Enum, AZStd::string("float"), "Value type",
                          { "bool", "float", "string", "vector3", "entity" }),
                      Parameter("value", ParameterKind::String, AZStd::string{}, "Value parsed using the selected type") },
                    "Sets a blackboard value" },
                { "Action", NodeCategory::Leaf, ChildCountRule::None,
                    { Parameter("name", ParameterKind::String, AZStd::string{}, "Action name") },
                    "Calls a scriptable action" },
                { "Condition", NodeCategory::Leaf, ChildCountRule::None,
                    { Parameter("name", ParameterKind::String, AZStd::string{}, "Condition name") },
                    "Calls a scriptable condition" },
                { "Log", NodeCategory::Leaf, ChildCountRule::None,
                    { Parameter("message", ParameterKind::String, AZStd::string{}, "Message to log") },
                    "Writes a message to the tree context" },
                { "MoveTo", NodeCategory::Leaf, ChildCountRule::None,
                    { Parameter("targetKey", ParameterKind::Key, AZStd::string{}, "Blackboard Vector3 or EntityId key"),
                      Parameter("acceptanceRadius", ParameterKind::Float, 0.5f, "Arrival radius"),
                      Parameter("speed", ParameterKind::Float, 4.0f, "Movement speed"),
                      Parameter("repathInterval", ParameterKind::Float, 0.5f, "Path refresh interval") },
                    "Follows a VoxelNav path to a blackboard target" },
            };
            return schemas;
        }
    } // namespace

    const AZStd::vector<NodeSchema>& NodeRegistry::GetAll()
    {
        return Schemas();
    }

    const NodeSchema* NodeRegistry::Find(AZStd::string_view type)
    {
        for (const NodeSchema& schema : Schemas())
        {
            if (schema.m_name == type)
            {
                return &schema;
            }
        }
        return nullptr;
    }

    bool NodeRegistry::ValidateChildCount(const NodeSchema& schema, size_t count, AZStd::string& error)
    {
        switch (schema.m_childCountRule)
        {
        case ChildCountRule::AtLeastOne:
            if (count > 0)
            {
                return true;
            }
            error = "requires at least one child";
            return false;
        case ChildCountRule::ExactlyOne:
            if (count == 1)
            {
                return true;
            }
            error = "requires exactly one child";
            return false;
        case ChildCountRule::None:
            if (count == 0)
            {
                return true;
            }
            error = "does not accept children";
            return false;
        }
        return false;
    }
} // namespace BehaviorTree
