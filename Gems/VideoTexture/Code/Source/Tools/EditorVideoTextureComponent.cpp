/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "EditorVideoTextureComponent.h"

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace VideoTexture
{
    void EditorVideoTextureComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<EditorVideoTextureComponent, EditorComponentBase>()
                ->Version(1)
                ->Field("Config", &EditorVideoTextureComponent::m_config);

            if (auto* editContext = serializeContext->GetEditContext())
            {
                editContext->Class<EditorVideoTextureComponent>(
                    "Video Texture", "Plays an MPEG-1 video onto a render target texture so materials can show video")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                        ->Attribute(AZ::Edit::Attributes::Category, "Graphics")
                        ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                        ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EditorVideoTextureComponent::m_config,
                        "Settings", "Video texture settings")
                        ->Attribute(AZ::Edit::Attributes::Visibility, AZ::Edit::PropertyVisibility::ShowChildrenOnly);
            }
        }
    }

    void EditorVideoTextureComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        VideoTextureComponent::GetProvidedServices(provided);
    }

    void EditorVideoTextureComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        VideoTextureComponent::GetIncompatibleServices(incompatible);
    }

    void EditorVideoTextureComponent::BuildGameEntity(AZ::Entity* gameEntity)
    {
        gameEntity->CreateComponent<VideoTextureComponent>(m_config);
    }
} // namespace VideoTexture
