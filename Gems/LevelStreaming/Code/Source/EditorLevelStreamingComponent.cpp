/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "EditorLevelStreamingComponent.h"

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

#include "LevelStreamingComponent.h"

namespace LevelStreaming
{
    void EditorLevelStreamingComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<EditorLevelStreamingComponent, EditorComponentBase>()
                ->Version(1)
                ->Field("Settings", &EditorLevelStreamingComponent::m_settings);

            if (auto* editContext = serializeContext->GetEditContext())
            {
                editContext->Class<EditorLevelStreamingComponent>(
                    "Level Streaming", "Streams the level's meshes in and out around the camera using a grid of chunks sized to their tallest object")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                        ->Attribute(AZ::Edit::Attributes::Category, "Graphics/Mesh")
                        ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                        ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EditorLevelStreamingComponent::m_settings,
                        "Settings", "Level streaming settings")
                        ->Attribute(AZ::Edit::Attributes::Visibility, AZ::Edit::PropertyVisibility::ShowChildrenOnly);
            }
        }
    }

    void EditorLevelStreamingComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        LevelStreamingComponent::GetProvidedServices(provided);
    }

    void EditorLevelStreamingComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        LevelStreamingComponent::GetIncompatibleServices(incompatible);
    }

    void EditorLevelStreamingComponent::BuildGameEntity(AZ::Entity* gameEntity)
    {
        gameEntity->CreateComponent<LevelStreamingComponent>(m_settings);
    }
} // namespace LevelStreaming
