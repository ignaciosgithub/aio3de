/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "ScriptField.h"

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/string/conversions.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace CSharpScripting
{
    void ScriptField::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<ScriptField>()
                ->Version(1)
                ->Field("Name", &ScriptField::m_name)
                ->Field("Type", &ScriptField::m_type)
                ->Field("FloatValue", &ScriptField::m_floatValue)
                ->Field("IntValue", &ScriptField::m_intValue)
                ->Field("BoolValue", &ScriptField::m_boolValue)
                ->Field("StringValue", &ScriptField::m_stringValue)
                ->Field("Vector3Value", &ScriptField::m_vector3Value)
                ->Field("EntityValue", &ScriptField::m_entityValue);

            if (auto* editContext = serializeContext->GetEditContext())
            {
                editContext->Class<ScriptField>("C# Script Field", "A field exposed by the C# script class")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                        ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ScriptField::m_floatValue, "Value", "Field value")
                        ->Attribute(AZ::Edit::Attributes::Visibility, &ScriptField::FloatVisibility)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ScriptField::m_intValue, "Value", "Field value")
                        ->Attribute(AZ::Edit::Attributes::Visibility, &ScriptField::IntVisibility)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ScriptField::m_boolValue, "Value", "Field value")
                        ->Attribute(AZ::Edit::Attributes::Visibility, &ScriptField::BoolVisibility)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ScriptField::m_stringValue, "Value", "Field value")
                        ->Attribute(AZ::Edit::Attributes::Visibility, &ScriptField::StringVisibility)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ScriptField::m_vector3Value, "Value", "Field value")
                        ->Attribute(AZ::Edit::Attributes::Visibility, &ScriptField::Vector3Visibility)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ScriptField::m_entityValue, "Value", "Field value")
                        ->Attribute(AZ::Edit::Attributes::Visibility, &ScriptField::EntityRefVisibility);
            }
        }
    }

    AZStd::string ScriptField::GetDisplayName() const
    {
        AZStd::string_view name(m_name);
        if (name.starts_with("m_") && name.size() > 2)
        {
            name.remove_prefix(2);
        }
        while (!name.empty() && name.front() == '_')
        {
            name.remove_prefix(1);
        }

        AZStd::string display;
        display.reserve(name.size() + 4);
        for (size_t i = 0; i < name.size(); ++i)
        {
            const char c = name[i];
            if (i == 0)
            {
                display += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            else
            {
                if (std::isupper(static_cast<unsigned char>(c)) && !std::isupper(static_cast<unsigned char>(name[i - 1])))
                {
                    display += ' ';
                }
                display += c;
            }
        }
        return display.empty() ? m_name : display;
    }

    AZStd::string ScriptField::ValueString() const
    {
        switch (static_cast<Type>(m_type))
        {
        case Type::Float:
            return AZStd::string::format("%.9g", m_floatValue);
        case Type::Int:
            return AZStd::string::format("%lld", static_cast<long long>(m_intValue));
        case Type::Bool:
            return m_boolValue ? "true" : "false";
        case Type::String:
            return m_stringValue;
        case Type::Vector3:
            return AZStd::string::format(
                "%.9g %.9g %.9g",
                static_cast<float>(m_vector3Value.GetX()),
                static_cast<float>(m_vector3Value.GetY()),
                static_cast<float>(m_vector3Value.GetZ()));
        case Type::EntityRef:
            return AZStd::string::format("%llu", static_cast<unsigned long long>(static_cast<AZ::u64>(m_entityValue)));
        }
        return {};
    }

    void ScriptField::SetValueFromString(const AZStd::string& value)
    {
        switch (static_cast<Type>(m_type))
        {
        case Type::Float:
            m_floatValue = AZStd::stof(value);
            break;
        case Type::Int:
            m_intValue = AZStd::stoll(value);
            break;
        case Type::Bool:
            m_boolValue = (value == "true" || value == "True" || value == "1");
            break;
        case Type::String:
            m_stringValue = value;
            break;
        case Type::Vector3:
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            if (azsscanf(value.c_str(), "%f %f %f", &x, &y, &z) == 3)
            {
                m_vector3Value.Set(x, y, z);
            }
            break;
        }
        case Type::EntityRef:
            m_entityValue = AZ::EntityId(strtoull(value.c_str(), nullptr, 10));
            break;
        }
    }

    AZ::Crc32 ScriptField::FloatVisibility() const
    {
        return m_type == static_cast<AZ::u32>(Type::Float) ? AZ::Edit::PropertyVisibility::Show : AZ::Edit::PropertyVisibility::Hide;
    }

    AZ::Crc32 ScriptField::IntVisibility() const
    {
        return m_type == static_cast<AZ::u32>(Type::Int) ? AZ::Edit::PropertyVisibility::Show : AZ::Edit::PropertyVisibility::Hide;
    }

    AZ::Crc32 ScriptField::BoolVisibility() const
    {
        return m_type == static_cast<AZ::u32>(Type::Bool) ? AZ::Edit::PropertyVisibility::Show : AZ::Edit::PropertyVisibility::Hide;
    }

    AZ::Crc32 ScriptField::StringVisibility() const
    {
        return m_type == static_cast<AZ::u32>(Type::String) ? AZ::Edit::PropertyVisibility::Show : AZ::Edit::PropertyVisibility::Hide;
    }

    AZ::Crc32 ScriptField::Vector3Visibility() const
    {
        return m_type == static_cast<AZ::u32>(Type::Vector3) ? AZ::Edit::PropertyVisibility::Show : AZ::Edit::PropertyVisibility::Hide;
    }

    AZ::Crc32 ScriptField::EntityRefVisibility() const
    {
        return m_type == static_cast<AZ::u32>(Type::EntityRef) ? AZ::Edit::PropertyVisibility::Show : AZ::Edit::PropertyVisibility::Hide;
    }
} // namespace CSharpScripting
