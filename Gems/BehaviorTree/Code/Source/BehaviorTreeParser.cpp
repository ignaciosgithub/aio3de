/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "BehaviorTreeParser.h"

#include "NodeRegistry.h"

#include <AzCore/IO/FileIO.h>
#include <AzCore/JSON/document.h>
#include <AzCore/Math/MathUtils.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/string/conversions.h>

namespace BehaviorTree
{
    namespace
    {
        using NodeOutcome = AZ::Outcome<NodeDefinition, AZStd::string>;

        AZStd::string ErrorAt(AZStd::string_view path, AZStd::string_view message)
        {
            return AZStd::string::format("%.*s: %.*s", aznumeric_cast<int>(path.size()), path.data(),
                aznumeric_cast<int>(message.size()), message.data());
        }

        bool ReadVector3(const rapidjson::Value& value, AZ::Vector3& outValue)
        {
            if (!value.IsArray() || value.Size() != 3)
            {
                return false;
            }
            for (rapidjson::SizeType index = 0; index < 3; ++index)
            {
                if (!value[index].IsNumber())
                {
                    return false;
                }
            }
            outValue = AZ::Vector3(value[0].GetFloat(), value[1].GetFloat(), value[2].GetFloat());
            return true;
        }

        bool ParseBlackboardValue(const rapidjson::Value& value, BlackboardType type, Blackboard::Value& outValue)
        {
            switch (type)
            {
            case BlackboardType::Bool:
                if (value.IsBool())
                {
                    outValue = value.GetBool();
                    return true;
                }
                return false;
            case BlackboardType::Float:
                if (value.IsNumber())
                {
                    outValue = value.GetFloat();
                    return true;
                }
                return false;
            case BlackboardType::String:
                if (value.IsString())
                {
                    outValue = AZStd::string(value.GetString(), value.GetStringLength());
                    return true;
                }
                return false;
            case BlackboardType::Vector3:
            {
                AZ::Vector3 vector;
                if (ReadVector3(value, vector))
                {
                    outValue = vector;
                    return true;
                }
                return false;
            }
            case BlackboardType::EntityId:
                if (value.IsUint64())
                {
                    outValue = AZ::EntityId(value.GetUint64());
                    return true;
                }
                return false;
            }
            return false;
        }

        bool ParseParameter(
            const rapidjson::Value& value, const NodeParameterSchema& schema, ParameterValue& outValue)
        {
            switch (schema.m_kind)
            {
            case ParameterKind::Bool:
                if (value.IsBool())
                {
                    outValue = value.GetBool();
                    return true;
                }
                return false;
            case ParameterKind::Float:
                if (value.IsNumber())
                {
                    outValue = value.GetFloat();
                    return true;
                }
                return false;
            case ParameterKind::Int:
                if (value.IsInt())
                {
                    outValue = value.GetInt();
                    return true;
                }
                return false;
            case ParameterKind::Vector3:
            {
                AZ::Vector3 vector;
                if (ReadVector3(value, vector))
                {
                    outValue = vector;
                    return true;
                }
                return false;
            }
            case ParameterKind::String:
            case ParameterKind::Key:
            case ParameterKind::Enum:
                if (!value.IsString())
                {
                    return false;
                }
                outValue = AZStd::string(value.GetString(), value.GetStringLength());
                if (schema.m_kind == ParameterKind::Enum)
                {
                    const AZStd::string& enumValue = AZStd::get<AZStd::string>(outValue);
                    return AZStd::find(schema.m_enumValues.begin(), schema.m_enumValues.end(), enumValue) !=
                        schema.m_enumValues.end();
                }
                return true;
            }
            return false;
        }

        const NodeParameterSchema* FindParameter(const NodeSchema& schema, AZStd::string_view name)
        {
            for (const NodeParameterSchema& parameter : schema.m_parameters)
            {
                if (parameter.m_name == name)
                {
                    return &parameter;
                }
            }
            return nullptr;
        }

        NodeOutcome ParseNode(const rapidjson::Value& value, AZStd::string path)
        {
            if (!value.IsObject())
            {
                return AZ::Failure(ErrorAt(path, "node must be an object"));
            }

            if (!value.HasMember("type") || !value["type"].IsString())
            {
                return AZ::Failure(ErrorAt(path, "missing string 'type'"));
            }

            NodeDefinition node;
            node.m_type = value["type"].GetString();
            const NodeSchema* schema = NodeRegistry::Find(node.m_type);
            if (!schema)
            {
                return AZ::Failure(ErrorAt(path, AZStd::string::format("unknown node type '%s'", node.m_type.c_str())));
            }

            const bool nameIsParameter = FindParameter(*schema, "name") != nullptr;
            if (value.HasMember("name") && !nameIsParameter)
            {
                if (!value["name"].IsString())
                {
                    return AZ::Failure(ErrorAt(path, "'name' must be a string"));
                }
                node.m_name = value["name"].GetString();
            }

            for (auto member = value.MemberBegin(); member != value.MemberEnd(); ++member)
            {
                const AZStd::string memberName(member->name.GetString(), member->name.GetStringLength());
                if (memberName == "type" || memberName == "children" || (memberName == "name" && !nameIsParameter))
                {
                    continue;
                }

                const NodeParameterSchema* parameter = FindParameter(*schema, memberName);
                if (!parameter)
                {
                    return AZ::Failure(ErrorAt(path, AZStd::string::format("unknown parameter '%s'", memberName.c_str())));
                }

                ParameterValue parsedValue = parameter->m_defaultValue;
                if (!ParseParameter(member->value, *parameter, parsedValue))
                {
                    return AZ::Failure(ErrorAt(
                        path, AZStd::string::format("parameter '%s' has the wrong type or value", memberName.c_str())));
                }
                node.m_parameters.insert_or_assign(memberName, AZStd::move(parsedValue));
            }

            for (const NodeParameterSchema& parameter : schema->m_parameters)
            {
                if (node.m_parameters.find(parameter.m_name) == node.m_parameters.end())
                {
                    node.m_parameters.emplace(parameter.m_name, parameter.m_defaultValue);
                }
            }

            if (value.HasMember("children"))
            {
                if (!value["children"].IsArray())
                {
                    return AZ::Failure(ErrorAt(path, "'children' must be an array"));
                }
                const rapidjson::Value& children = value["children"];
                node.m_children.reserve(children.Size());
                for (rapidjson::SizeType index = 0; index < children.Size(); ++index)
                {
                    const AZStd::string childPath = AZStd::string::format("%s/children[%u]", path.c_str(), index);
                    NodeOutcome child = ParseNode(children[index], childPath);
                    if (!child.IsSuccess())
                    {
                        return AZ::Failure(child.GetError());
                    }
                    node.m_children.push_back(child.TakeValue());
                }
            }

            AZStd::string childError;
            if (!NodeRegistry::ValidateChildCount(*schema, node.m_children.size(), childError))
            {
                return AZ::Failure(ErrorAt(path, childError));
            }
            return AZ::Success(AZStd::move(node));
        }

        bool ParseBlackboard(const rapidjson::Value& value, BehaviorTreeDefinition& definition, AZStd::string& error)
        {
            if (!value.IsObject())
            {
                error = "blackboard must be an object";
                return false;
            }

            for (auto member = value.MemberBegin(); member != value.MemberEnd(); ++member)
            {
                const AZStd::string key(member->name.GetString(), member->name.GetStringLength());
                const rapidjson::Value& entryValue = member->value;
                if (!entryValue.IsObject() || !entryValue.HasMember("type") || !entryValue["type"].IsString())
                {
                    error = AZStd::string::format("blackboard.%s: expected an object with a string 'type'", key.c_str());
                    return false;
                }

                BlackboardEntry entry;
                const AZStd::string typeName = entryValue["type"].GetString();
                if (typeName == "bool")
                {
                    entry.m_type = BlackboardType::Bool;
                    entry.m_value = false;
                }
                else if (typeName == "float")
                {
                    entry.m_type = BlackboardType::Float;
                    entry.m_value = 0.0f;
                }
                else if (typeName == "string")
                {
                    entry.m_type = BlackboardType::String;
                    entry.m_value = AZStd::string{};
                }
                else if (typeName == "vector3")
                {
                    entry.m_type = BlackboardType::Vector3;
                    entry.m_value = AZ::Vector3::CreateZero();
                }
                else if (typeName == "entity")
                {
                    entry.m_type = BlackboardType::EntityId;
                    entry.m_value = AZ::EntityId{};
                }
                else
                {
                    error = AZStd::string::format("blackboard.%s: unknown type '%s'", key.c_str(), typeName.c_str());
                    return false;
                }

                if (entryValue.HasMember("value"))
                {
                    if (!ParseBlackboardValue(entryValue["value"], entry.m_type, entry.m_value))
                    {
                        error = AZStd::string::format("blackboard.%s.value: wrong type", key.c_str());
                        return false;
                    }
                    entry.m_hasExplicitValue = true;
                }

                for (auto field = entryValue.MemberBegin(); field != entryValue.MemberEnd(); ++field)
                {
                    const AZStd::string fieldName(field->name.GetString(), field->name.GetStringLength());
                    if (fieldName != "type" && fieldName != "value")
                    {
                        error = AZStd::string::format("blackboard.%s: unknown field '%s'", key.c_str(), fieldName.c_str());
                        return false;
                    }
                }
                definition.m_blackboard.emplace(key, AZStd::move(entry));
            }
            return true;
        }
    } // namespace

    BehaviorTreeParser::ParseOutcome BehaviorTreeParser::Parse(AZStd::string_view json)
    {
        AZStd::string jsonText(json.data(), json.size());
        rapidjson::Document document;
        document.Parse(jsonText.c_str());
        if (document.HasParseError())
        {
            return AZ::Failure(
                AZStd::string::format("JSON parse error at byte %u", aznumeric_cast<AZ::u32>(document.GetErrorOffset())));
        }
        if (!document.IsObject())
        {
            return AZ::Failure(AZStd::string("root: document must be an object"));
        }

        BehaviorTreeDefinition definition;
        if (document.HasMember("version"))
        {
            if (!document["version"].IsUint() || document["version"].GetUint() != 1)
            {
                return AZ::Failure(AZStd::string("version: only version 1 is supported"));
            }
            definition.m_version = document["version"].GetUint();
        }
        if (document.HasMember("loop"))
        {
            if (!document["loop"].IsBool())
            {
                return AZ::Failure(AZStd::string("loop: must be a boolean"));
            }
            definition.m_loop = document["loop"].GetBool();
        }
        if (document.HasMember("blackboard"))
        {
            AZStd::string error;
            if (!ParseBlackboard(document["blackboard"], definition, error))
            {
                return AZ::Failure(AZStd::move(error));
            }
        }
        if (!document.HasMember("root"))
        {
            return AZ::Failure(AZStd::string("root: node is required"));
        }

        NodeOutcome root = ParseNode(document["root"], "root");
        if (!root.IsSuccess())
        {
            return AZ::Failure(root.GetError());
        }
        definition.m_root = root.TakeValue();

        for (auto member = document.MemberBegin(); member != document.MemberEnd(); ++member)
        {
            const AZStd::string name(member->name.GetString(), member->name.GetStringLength());
            if (name != "version" && name != "loop" && name != "blackboard" && name != "root")
            {
                return AZ::Failure(AZStd::string::format("document: unknown field '%s'", name.c_str()));
            }
        }
        return AZ::Success(AZStd::move(definition));
    }

    BehaviorTreeParser::ParseOutcome BehaviorTreeParser::LoadFromFile(const AZStd::string& path)
    {
        AZ::IO::FileIOBase* fileIo = AZ::IO::FileIOBase::GetInstance();
        if (!fileIo)
        {
            return AZ::Failure(AZStd::string::format("%s: no FileIO instance", path.c_str()));
        }

        AZ::IO::HandleType handle = AZ::IO::InvalidHandle;
        if (!fileIo->Open(path.c_str(), AZ::IO::OpenMode::ModeRead, handle))
        {
            return AZ::Failure(AZStd::string::format("%s: cannot open file", path.c_str()));
        }
        AZ::u64 size = 0;
        fileIo->Size(handle, size);
        AZStd::string data;
        data.resize_no_construct(size);
        const bool readSucceeded = fileIo->Read(handle, data.data(), size);
        fileIo->Close(handle);
        if (!readSucceeded)
        {
            return AZ::Failure(AZStd::string::format("%s: failed to read file", path.c_str()));
        }
        return Parse(data);
    }
} // namespace BehaviorTree
