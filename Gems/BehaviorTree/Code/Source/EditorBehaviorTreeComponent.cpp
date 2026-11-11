/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "EditorBehaviorTreeComponent.h"

#include "BehaviorTreeEditorWindow.h"

#include "BehaviorTreeComponent.h"

#include <AzCore/Component/TickBus.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzToolsFramework/API/ToolsApplicationAPI.h>
#include <AzToolsFramework/API/ViewPaneOptions.h>

namespace BehaviorTree
{
    void EditorBehaviorTreeComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<EditorBehaviorTreeComponent, EditorComponentBase>()
                ->Version(1)
                ->Field("TreeFile", &EditorBehaviorTreeComponent::m_treeFile)
                ->Field("TickInterval", &EditorBehaviorTreeComponent::m_tickInterval)
                ->Field("StartActive", &EditorBehaviorTreeComponent::m_startActive)
                ->Field("DebugLogTransitions", &EditorBehaviorTreeComponent::m_debugLogTransitions);

            if (AZ::EditContext* editContext = serializeContext->GetEditContext())
            {
                editContext->Class<EditorBehaviorTreeComponent>("Behavior Tree", "Runs a data-driven behavior tree for this entity")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                        ->Attribute(AZ::Edit::Attributes::Category, "AI")
                        ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                        ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EditorBehaviorTreeComponent::m_treeFile, "Tree file",
                        "Project-relative .behaviortree.json path")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EditorBehaviorTreeComponent::m_tickInterval, "Tick interval (s)",
                        "Zero ticks every frame")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EditorBehaviorTreeComponent::m_startActive, "Start active",
                        "Load and run the tree when the entity activates")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EditorBehaviorTreeComponent::m_debugLogTransitions,
                        "Debug log transitions", "Log tree status changes")
                    ->UIElement(AZ::Edit::UIHandlers::Button, "", "Open this behavior tree in the Behavior Tree Editor")
                        ->Attribute(AZ::Edit::Attributes::ButtonText, "Open in Behavior Tree Editor")
                        ->Attribute(AZ::Edit::Attributes::ChangeNotify, &EditorBehaviorTreeComponent::OpenEditor);
            }
        }
    }

    void EditorBehaviorTreeComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("BehaviorTreeService"));
    }

    void EditorBehaviorTreeComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("BehaviorTreeService"));
    }

    void EditorBehaviorTreeComponent::Activate()
    {
        EditorComponentBase::Activate();
    }

    void EditorBehaviorTreeComponent::Deactivate()
    {
        EditorComponentBase::Deactivate();
    }

    void EditorBehaviorTreeComponent::BuildGameEntity(AZ::Entity* gameEntity)
    {
        if (BehaviorTreeComponent* runtimeComponent = gameEntity->CreateComponent<BehaviorTreeComponent>())
        {
            runtimeComponent->SetConfiguration(m_treeFile, m_tickInterval, m_startActive, m_debugLogTransitions);
        }
    }

    AZ::Crc32 EditorBehaviorTreeComponent::OpenEditor()
    {
        AzToolsFramework::OpenViewPane("Behavior Tree Editor");
        const AZStd::string treeFile = m_treeFile;
        AZ::TickBus::QueueFunction(
            [treeFile]()
            {
                BehaviorTreeEditorRequestBus::Broadcast(
                    &BehaviorTreeEditorRequests::OpenTreeFile, treeFile);
            });
        return AZ::Edit::PropertyRefreshLevels::None;
    }
} // namespace BehaviorTree
