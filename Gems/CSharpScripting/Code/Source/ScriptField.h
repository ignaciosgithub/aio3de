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
#include <AzCore/Memory/SystemAllocator.h>
#include <AzCore/RTTI/TypeInfoSimple.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>

namespace AZ
{
    class ReflectContext;
}

namespace CSharpScripting
{
    //! An inspectable C# script field (public field or [SerializeField]) exposed in the Inspector.
    struct ScriptField
    {
        AZ_TYPE_INFO(ScriptField, "{9C3D5E7F-4A61-48CA-BD2E-F3A4B5C6D7E8}");
        AZ_CLASS_ALLOCATOR(ScriptField, AZ::SystemAllocator);

        enum class Type : AZ::u32
        {
            Float,
            Int,
            Bool,
            String,
            Vector3,
            EntityRef,
        };

        static void Reflect(AZ::ReflectContext* context);

        //! Field name as declared in C# (used to match values back to the field).
        AZStd::string m_name;
        AZ::u32 m_type = static_cast<AZ::u32>(Type::Float);
        float m_floatValue = 0.0f;
        AZ::s64 m_intValue = 0;
        bool m_boolValue = false;
        AZStd::string m_stringValue;
        AZ::Vector3 m_vector3Value = AZ::Vector3::CreateZero();
        AZ::EntityId m_entityValue;

        //! Unity-style display name ("_moveSpeed" -> "Move Speed").
        AZStd::string GetDisplayName() const;

        //! Value serialized as the string form SetScriptField expects.
        AZStd::string ValueString() const;
        //! Parses a value string produced by the managed DescribeScript into the typed slot.
        void SetValueFromString(const AZStd::string& value);

        AZ::Crc32 FloatVisibility() const;
        AZ::Crc32 IntVisibility() const;
        AZ::Crc32 BoolVisibility() const;
        AZ::Crc32 StringVisibility() const;
        AZ::Crc32 Vector3Visibility() const;
        AZ::Crc32 EntityRefVisibility() const;
    };

    using ScriptFieldList = AZStd::vector<ScriptField>;
} // namespace CSharpScripting
