/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include "BehaviorTreeDefinition.h"

#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string_view.h>

namespace BehaviorTree
{
    enum class NodeCategory : AZ::u8
    {
        Composite,
        Decorator,
        Leaf
    };

    enum class ChildCountRule : AZ::u8
    {
        AtLeastOne,
        ExactlyOne,
        None
    };

    enum class ParameterKind : AZ::u8
    {
        Bool,
        Float,
        Int,
        String,
        Key,
        Vector3,
        Enum
    };

    struct NodeParameterSchema
    {
        AZStd::string m_name;
        ParameterKind m_kind = ParameterKind::String;
        ParameterValue m_defaultValue = AZStd::string{};
        AZStd::vector<AZStd::string> m_enumValues;
        AZStd::string m_description;
    };

    struct NodeSchema
    {
        AZStd::string m_name;
        NodeCategory m_category = NodeCategory::Leaf;
        ChildCountRule m_childCountRule = ChildCountRule::None;
        AZStd::vector<NodeParameterSchema> m_parameters;
        AZStd::string m_description;
    };

    class NodeRegistry
    {
    public:
        static const AZStd::vector<NodeSchema>& GetAll();
        static const NodeSchema* Find(AZStd::string_view type);
        static bool ValidateChildCount(const NodeSchema& schema, size_t count, AZStd::string& error);
    };
} // namespace BehaviorTree
