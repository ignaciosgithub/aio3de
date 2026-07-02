/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "EditorSoftBodyComponent.h"

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

#include "SoftBodyComponent.h"

namespace SoftBodyPhysics
{
    void EditorSoftBodyComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<EditorSoftBodyComponent, EditorComponentBase>()
                ->Version(1)
                ->Field("Settings", &EditorSoftBodyComponent::m_settings);

            if (auto* editContext = serializeContext->GetEditContext())
            {
                editContext->Class<EditorSoftBodyComponent>(
                    "Soft Body", "Simulates the entity's mesh as an XPBD soft body in game mode")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                        ->Attribute(AZ::Edit::Attributes::Category, "Physics")
                        ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                        ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EditorSoftBodyComponent::m_settings,
                        "Settings", "Soft body simulation settings")
                        ->Attribute(AZ::Edit::Attributes::Visibility, AZ::Edit::PropertyVisibility::ShowChildrenOnly);
            }
        }
    }

    void EditorSoftBodyComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        SoftBodyComponent::GetProvidedServices(provided);
    }

    void EditorSoftBodyComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        SoftBodyComponent::GetIncompatibleServices(incompatible);
    }

    void EditorSoftBodyComponent::GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required)
    {
        SoftBodyComponent::GetRequiredServices(required);
    }

    void EditorSoftBodyComponent::BuildGameEntity(AZ::Entity* gameEntity)
    {
        gameEntity->CreateComponent<SoftBodyComponent>(m_settings);
    }
} // namespace SoftBodyPhysics
