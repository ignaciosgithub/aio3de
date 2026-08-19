/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "EditorReplayTrackerComponent.h"

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace Replay
{
    void EditorReplayTrackerComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<EditorReplayTrackerComponent, EditorComponentBase>()
                ->Version(1)
                ->Field("Config", &EditorReplayTrackerComponent::m_config);

            if (auto* editContext = serializeContext->GetEditContext())
            {
                editContext->Class<EditorReplayTrackerComponent>(
                    "Replay Tracker", "Records this entity's transform into demo files for the replay system")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                        ->Attribute(AZ::Edit::Attributes::Category, "Gameplay")
                        ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                        ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EditorReplayTrackerComponent::m_config,
                        "Settings", "Replay tracker settings")
                        ->Attribute(AZ::Edit::Attributes::Visibility, AZ::Edit::PropertyVisibility::ShowChildrenOnly);
            }
        }
    }

    void EditorReplayTrackerComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        ReplayTrackerComponent::GetProvidedServices(provided);
    }

    void EditorReplayTrackerComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        ReplayTrackerComponent::GetIncompatibleServices(incompatible);
    }

    void EditorReplayTrackerComponent::BuildGameEntity(AZ::Entity* gameEntity)
    {
        gameEntity->CreateComponent<ReplayTrackerComponent>(m_config);
    }
} // namespace Replay
