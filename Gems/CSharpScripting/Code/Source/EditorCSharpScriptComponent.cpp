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
                ->Version(1)
                ->Field("ClassName", &EditorCSharpScriptComponent::m_className);

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
        return AZ::Edit::PropertyRefreshLevels::None;
    }

    void EditorCSharpScriptComponent::BuildGameEntity(AZ::Entity* gameEntity)
    {
        gameEntity->CreateComponent<CSharpScriptComponent>(m_className);
    }
} // namespace CSharpScripting
