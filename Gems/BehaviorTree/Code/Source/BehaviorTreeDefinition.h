/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include "Blackboard.h"

#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/containers/variant.h>

namespace BehaviorTree
{
    using ParameterValue = AZStd::variant<bool, AZ::s32, float, AZStd::string, AZ::Vector3>;

    struct NodeDefinition
    {
        AZStd::string m_type;
        AZStd::string m_name;
        AZStd::unordered_map<AZStd::string, ParameterValue> m_parameters;
        AZStd::vector<NodeDefinition> m_children;

        const ParameterValue* FindParameter(const AZStd::string& name) const
        {
            auto it = m_parameters.find(name);
            return it == m_parameters.end() ? nullptr : &it->second;
        }
    };

    struct BlackboardEntry
    {
        BlackboardType m_type = BlackboardType::Float;
        Blackboard::Value m_value = 0.0f;
        bool m_hasExplicitValue = false;
    };

    struct BehaviorTreeDefinition
    {
        AZ::u32 m_version = 1;
        bool m_loop = true;
        AZStd::unordered_map<AZStd::string, BlackboardEntry> m_blackboard;
        NodeDefinition m_root;
    };
} // namespace BehaviorTree
