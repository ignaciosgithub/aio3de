/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "Blackboard.h"

namespace BehaviorTree
{
    void Blackboard::Set(const AZStd::string& key, Value value)
    {
        m_values.insert_or_assign(key, AZStd::move(value));
    }

    void Blackboard::SetBool(const AZStd::string& key, bool value)
    {
        Set(key, value);
    }

    void Blackboard::SetFloat(const AZStd::string& key, float value)
    {
        Set(key, value);
    }

    void Blackboard::SetString(const AZStd::string& key, const AZStd::string& value)
    {
        Set(key, value);
    }

    void Blackboard::SetVector3(const AZStd::string& key, const AZ::Vector3& value)
    {
        Set(key, value);
    }

    void Blackboard::SetEntityId(const AZStd::string& key, const AZ::EntityId& value)
    {
        Set(key, value);
    }

    bool Blackboard::Has(const AZStd::string& key) const
    {
        return m_values.find(key) != m_values.end();
    }

    const Blackboard::Value* Blackboard::Find(const AZStd::string& key) const
    {
        auto it = m_values.find(key);
        return it == m_values.end() ? nullptr : &it->second;
    }

    Blackboard::Value* Blackboard::Find(const AZStd::string& key)
    {
        auto it = m_values.find(key);
        return it == m_values.end() ? nullptr : &it->second;
    }

    bool Blackboard::GetBool(const AZStd::string& key, bool defaultValue) const
    {
        const Value* value = Find(key);
        const bool* typedValue = value ? AZStd::get_if<bool>(value) : nullptr;
        return typedValue ? *typedValue : defaultValue;
    }

    float Blackboard::GetFloat(const AZStd::string& key, float defaultValue) const
    {
        const Value* value = Find(key);
        const float* typedValue = value ? AZStd::get_if<float>(value) : nullptr;
        return typedValue ? *typedValue : defaultValue;
    }

    AZStd::string Blackboard::GetString(const AZStd::string& key, const AZStd::string& defaultValue) const
    {
        const Value* value = Find(key);
        const AZStd::string* typedValue = value ? AZStd::get_if<AZStd::string>(value) : nullptr;
        return typedValue ? *typedValue : defaultValue;
    }

    AZ::Vector3 Blackboard::GetVector3(const AZStd::string& key, const AZ::Vector3& defaultValue) const
    {
        const Value* value = Find(key);
        const AZ::Vector3* typedValue = value ? AZStd::get_if<AZ::Vector3>(value) : nullptr;
        return typedValue ? *typedValue : defaultValue;
    }

    AZ::EntityId Blackboard::GetEntityId(const AZStd::string& key, const AZ::EntityId& defaultValue) const
    {
        const Value* value = Find(key);
        const AZ::EntityId* typedValue = value ? AZStd::get_if<AZ::EntityId>(value) : nullptr;
        return typedValue ? *typedValue : defaultValue;
    }

    bool Blackboard::CompareFloat(const AZStd::string& key, CompareOp op, float value) const
    {
        const Value* found = Find(key);
        const float* typedValue = found ? AZStd::get_if<float>(found) : nullptr;
        if (!typedValue)
        {
            return op == CompareOp::NotEqual;
        }

        switch (op)
        {
        case CompareOp::Exists:
            return true;
        case CompareOp::Equal:
            return *typedValue == value;
        case CompareOp::NotEqual:
            return *typedValue != value;
        case CompareOp::Less:
            return *typedValue < value;
        case CompareOp::LessEqual:
            return *typedValue <= value;
        case CompareOp::Greater:
            return *typedValue > value;
        case CompareOp::GreaterEqual:
            return *typedValue >= value;
        case CompareOp::IsTrue:
        case CompareOp::IsFalse:
            return false;
        }
        return false;
    }

    void Blackboard::Clear()
    {
        m_values.clear();
    }

    const AZStd::unordered_map<AZStd::string, Blackboard::Value>& Blackboard::GetValues() const
    {
        return m_values;
    }

    BlackboardType Blackboard::GetType(const Value& value)
    {
        switch (value.index())
        {
        case 0:
            return BlackboardType::Bool;
        case 1:
            return BlackboardType::Float;
        case 2:
            return BlackboardType::String;
        case 3:
            return BlackboardType::Vector3;
        default:
            return BlackboardType::EntityId;
        }
    }
} // namespace BehaviorTree
