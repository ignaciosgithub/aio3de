/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "BehaviorTreeWriter.h"

#include "NodeRegistry.h"

#include <AzCore/JSON/prettywriter.h>
#include <AzCore/JSON/stringbuffer.h>
#include <AzCore/std/sort.h>

namespace BehaviorTree
{
    namespace
    {
        using JsonWriter = rapidjson::PrettyWriter<rapidjson::StringBuffer>;

        void WriteVector3(JsonWriter& writer, const AZ::Vector3& value)
        {
            writer.StartArray();
            writer.Double(value.GetX());
            writer.Double(value.GetY());
            writer.Double(value.GetZ());
            writer.EndArray();
        }

        void WriteParameter(JsonWriter& writer, const ParameterValue& value)
        {
            AZStd::visit(
                [&writer](const auto& typedValue)
                {
                    using ValueType = AZStd::remove_cvref_t<decltype(typedValue)>;
                    if constexpr (AZStd::is_same_v<ValueType, bool>)
                    {
                        writer.Bool(typedValue);
                    }
                    else if constexpr (AZStd::is_same_v<ValueType, AZ::s32>)
                    {
                        writer.Int(typedValue);
                    }
                    else if constexpr (AZStd::is_same_v<ValueType, float>)
                    {
                        writer.Double(typedValue);
                    }
                    else if constexpr (AZStd::is_same_v<ValueType, AZStd::string>)
                    {
                        writer.String(typedValue.c_str(), aznumeric_cast<rapidjson::SizeType>(typedValue.size()));
                    }
                    else if constexpr (AZStd::is_same_v<ValueType, AZ::Vector3>)
                    {
                        WriteVector3(writer, typedValue);
                    }
                },
                value);
        }

        void WriteNode(JsonWriter& writer, const NodeDefinition& node)
        {
            writer.StartObject();
            writer.Key("type");
            writer.String(node.m_type.c_str(), aznumeric_cast<rapidjson::SizeType>(node.m_type.size()));
            const NodeSchema* schema = NodeRegistry::Find(node.m_type);
            const bool nameIsParameter = schema &&
                AZStd::any_of(schema->m_parameters.begin(), schema->m_parameters.end(),
                    [](const NodeParameterSchema& parameter) { return parameter.m_name == "name"; });
            if (!nameIsParameter && !node.m_name.empty())
            {
                writer.Key("name");
                writer.String(node.m_name.c_str(), aznumeric_cast<rapidjson::SizeType>(node.m_name.size()));
            }
            if (schema)
            {
                for (const NodeParameterSchema& parameter : schema->m_parameters)
                {
                    auto it = node.m_parameters.find(parameter.m_name);
                    if (it != node.m_parameters.end())
                    {
                        writer.Key(parameter.m_name.c_str(),
                            aznumeric_cast<rapidjson::SizeType>(parameter.m_name.size()));
                        WriteParameter(writer, it->second);
                    }
                }
            }
            if (!node.m_children.empty())
            {
                writer.Key("children");
                writer.StartArray();
                for (const NodeDefinition& child : node.m_children)
                {
                    WriteNode(writer, child);
                }
                writer.EndArray();
            }
            writer.EndObject();
        }

        void WriteBlackboardValue(JsonWriter& writer, BlackboardType type, const Blackboard::Value& value)
        {
            switch (type)
            {
            case BlackboardType::Bool:
                writer.Bool(AZStd::get<bool>(value));
                break;
            case BlackboardType::Float:
                writer.Double(AZStd::get<float>(value));
                break;
            case BlackboardType::String:
            {
                const AZStd::string& stringValue = AZStd::get<AZStd::string>(value);
                writer.String(stringValue.c_str(), aznumeric_cast<rapidjson::SizeType>(stringValue.size()));
                break;
            }
            case BlackboardType::Vector3:
                WriteVector3(writer, AZStd::get<AZ::Vector3>(value));
                break;
            case BlackboardType::EntityId:
                writer.Uint64(static_cast<AZ::u64>(AZStd::get<AZ::EntityId>(value)));
                break;
            }
        }
    } // namespace

    AZStd::string BehaviorTreeWriter::Write(const BehaviorTreeDefinition& definition)
    {
        rapidjson::StringBuffer buffer;
        JsonWriter writer(buffer);
        writer.StartObject();
        writer.Key("version");
        writer.Uint(definition.m_version);
        writer.Key("loop");
        writer.Bool(definition.m_loop);

        writer.Key("blackboard");
        writer.StartObject();
        AZStd::vector<AZStd::string> keys;
        keys.reserve(definition.m_blackboard.size());
        for (const auto& [key, entry] : definition.m_blackboard)
        {
            AZ_UNUSED(entry);
            keys.push_back(key);
        }
        AZStd::sort(keys.begin(), keys.end());
        for (const AZStd::string& key : keys)
        {
            const BlackboardEntry& entry = definition.m_blackboard.at(key);
            writer.Key(key.c_str(), aznumeric_cast<rapidjson::SizeType>(key.size()));
            writer.StartObject();
            writer.Key("type");
            switch (entry.m_type)
            {
            case BlackboardType::Bool:
                writer.String("bool");
                break;
            case BlackboardType::Float:
                writer.String("float");
                break;
            case BlackboardType::String:
                writer.String("string");
                break;
            case BlackboardType::Vector3:
                writer.String("vector3");
                break;
            case BlackboardType::EntityId:
                writer.String("entity");
                break;
            }
            if (entry.m_hasExplicitValue)
            {
                writer.Key("value");
                WriteBlackboardValue(writer, entry.m_type, entry.m_value);
            }
            writer.EndObject();
        }
        writer.EndObject();

        writer.Key("root");
        WriteNode(writer, definition.m_root);
        writer.EndObject();
        return AZStd::string(buffer.GetString(), buffer.GetSize());
    }
} // namespace BehaviorTree
