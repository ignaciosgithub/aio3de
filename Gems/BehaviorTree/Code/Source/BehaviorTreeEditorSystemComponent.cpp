/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "BehaviorTreeEditorSystemComponent.h"

#include "BehaviorTreeEditorWindow.h"

#include <AzCore/Serialization/SerializeContext.h>
#include <AzToolsFramework/API/ViewPaneOptions.h>
#include <LyViewPaneNames.h>

namespace BehaviorTree
{
    void BehaviorTreeEditorSystemComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<BehaviorTreeEditorSystemComponent, AZ::Component>()->Version(1);
        }
    }

    void BehaviorTreeEditorSystemComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("BehaviorTreeEditorService"));
    }

    void BehaviorTreeEditorSystemComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("BehaviorTreeEditorService"));
    }

    void BehaviorTreeEditorSystemComponent::Activate()
    {
        AzToolsFramework::EditorEvents::Bus::Handler::BusConnect();
    }

    void BehaviorTreeEditorSystemComponent::Deactivate()
    {
        AzToolsFramework::EditorEvents::Bus::Handler::BusDisconnect();
    }

    void BehaviorTreeEditorSystemComponent::NotifyRegisterViews()
    {
        QtViewOptions options;
        options.canHaveMultipleInstances = false;
        options.showInMenu = true;
        options.showOnToolsToolbar = true;
        AzToolsFramework::RegisterViewPane<BehaviorTreeEditorWindow>(
            "Behavior Tree Editor", LyViewPane::CategoryTools, options);
    }
} // namespace BehaviorTree
