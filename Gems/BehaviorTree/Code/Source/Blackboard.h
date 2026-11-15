/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Component/EntityId.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/optional.h>
#include <AzCore/std/string/string.h>
#include <AzCore/std/containers/variant.h>

namespace BehaviorTree
{
    enum class BlackboardType : AZ::u8
    {
        Bool,
        Float,
        String,
        Vector3,
        EntityId
    };

    enum class CompareOp : AZ::u8
    {
        Exists,
        Equal,
        NotEqual,
        Less,
        LessEqual,
        Greater,
        GreaterEqual,
        IsTrue,
        IsFalse
    };

    class Blackboard
    {
    public:
        using Value = AZStd::variant<bool, float, AZStd::string, AZ::Vector3, AZ::EntityId>;

        void Set(const AZStd::string& key, Value value);
        void SetBool(const AZStd::string& key, bool value);
        void SetFloat(const AZStd::string& key, float value);
        void SetString(const AZStd::string& key, const AZStd::string& value);
        void SetVector3(const AZStd::string& key, const AZ::Vector3& value);
        void SetEntityId(const AZStd::string& key, const AZ::EntityId& value);

        bool Has(const AZStd::string& key) const;
        const Value* Find(const AZStd::string& key) const;
        Value* Find(const AZStd::string& key);

        bool GetBool(const AZStd::string& key, bool defaultValue = false) const;
        float GetFloat(const AZStd::string& key, float defaultValue = 0.0f) const;
        AZStd::string GetString(const AZStd::string& key, const AZStd::string& defaultValue = {}) const;
        AZ::Vector3 GetVector3(const AZStd::string& key, const AZ::Vector3& defaultValue = AZ::Vector3::CreateZero()) const;
        AZ::EntityId GetEntityId(const AZStd::string& key, const AZ::EntityId& defaultValue = AZ::EntityId()) const;
        bool CompareFloat(const AZStd::string& key, CompareOp op, float value) const;

        void Clear();
        const AZStd::unordered_map<AZStd::string, Value>& GetValues() const;
        static BlackboardType GetType(const Value& value);

    private:
        AZStd::unordered_map<AZStd::string, Value> m_values;
    };
} // namespace BehaviorTree
