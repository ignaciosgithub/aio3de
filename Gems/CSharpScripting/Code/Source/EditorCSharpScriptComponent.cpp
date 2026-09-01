/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "EditorCSharpScriptComponent.h"

#include "CSharpScriptComponent.h"
#include "ScriptHost.h"

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace CSharpScripting
{
    void EditorCSharpScriptComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<EditorCSharpScriptComponent, EditorComponentBase>()
                ->Version(2)
                ->Field("ClassName", &EditorCSharpScriptComponent::m_className)
                ->Field("Fields", &EditorCSharpScriptComponent::m_fields);

            if (auto* editContext = serializeContext->GetEditContext())
            {
                editContext->Class<EditorCSharpScriptComponent>(
                    "C# Script", "Runs a C# behaviour class from <project>/Scripts on this entity in game mode")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                        ->Attribute(AZ::Edit::Attributes::Category, "Scripting")
                        ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                        ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EditorCSharpScriptComponent::m_className,
                        "Class name", "The C# class to instantiate (namespace-qualified if declared in a namespace); "
                        "it must derive from AIO3DE.ScriptComponent")
                        ->Attribute(AZ::Edit::Attributes::ChangeNotify, &EditorCSharpScriptComponent::OnClassNameChanged)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EditorCSharpScriptComponent::m_fields,
                        "Fields", "Public and [SerializeField] fields of the script class (float, int, bool, string, Vector3)")
                        ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                        ->Attribute(AZ::Edit::Attributes::ContainerCanBeModified, false)
                        ->ElementAttribute(AZ::Edit::Attributes::NameLabelOverride, &ScriptField::GetDisplayName)
                    ->UIElement(AZ::Edit::UIHandlers::Button, "Rebuild scripts", "Recompile and reload all C# scripts now")
                        ->Attribute(AZ::Edit::Attributes::ButtonText, "Rebuild scripts")
                        ->Attribute(AZ::Edit::Attributes::ChangeNotify, &EditorCSharpScriptComponent::OnRebuildPressed);
            }
        }
    }

    void EditorCSharpScriptComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        CSharpScriptComponent::GetProvidedServices(provided);
    }

    void EditorCSharpScriptComponent::GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required)
    {
        CSharpScriptComponent::GetRequiredServices(required);
    }

    AZ::Crc32 EditorCSharpScriptComponent::OnRebuildPressed()
    {
        ScriptHost::Instance().RebuildScripts();
        RefreshFields();
        return AZ::Edit::PropertyRefreshLevels::EntireTree;
    }

    AZ::Crc32 EditorCSharpScriptComponent::OnClassNameChanged()
    {
        RefreshFields();
        return AZ::Edit::PropertyRefreshLevels::EntireTree;
    }

    void EditorCSharpScriptComponent::RefreshFields()
    {
        if (m_className.empty())
        {
            m_fields.clear();
            return;
        }

        ScriptFieldList declared;
        if (!ScriptHost::Instance().DescribeScript(m_className, declared))
        {
            // Keep the serialized values (e.g. scripts temporarily failing to build);
            // they are re-merged on the next successful refresh.
            return;
        }

        // Keep the current value of fields that still exist with the same type; take
        // the script's default for new ones.
        for (ScriptField& field : declared)
        {
            for (const ScriptField& existing : m_fields)
            {
                if (existing.m_name == field.m_name && existing.m_type == field.m_type)
                {
                    field = existing;
                    break;
                }
            }
        }
        m_fields = AZStd::move(declared);
    }

    void EditorCSharpScriptComponent::Activate()
    {
        EditorComponentBase::Activate();
        if (!m_className.empty())
        {
            RefreshFields();
        }
    }

    void EditorCSharpScriptComponent::BuildGameEntity(AZ::Entity* gameEntity)
    {
        gameEntity->CreateComponent<CSharpScriptComponent>(m_className, m_fields);
    }
} // namespace CSharpScripting
