/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "EditorFluidVolumeComponent.h"

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

#include "FluidVolumeComponent.h"

namespace FluidDynamics
{
    void EditorFluidVolumeComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<EditorFluidVolumeComponent, EditorComponentBase>()
                ->Version(1)
                ->Field("Settings", &EditorFluidVolumeComponent::m_settings);

            if (auto* editContext = serializeContext->GetEditContext())
            {
                editContext->Class<EditorFluidVolumeComponent>(
                    "Fluid Volume", "Simulates a particle fluid (water, honey, ...) inside a container box in game mode")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                        ->Attribute(AZ::Edit::Attributes::Category, "Physics")
                        ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                        ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EditorFluidVolumeComponent::m_settings,
                        "Settings", "Fluid simulation settings")
                        ->Attribute(AZ::Edit::Attributes::Visibility, AZ::Edit::PropertyVisibility::ShowChildrenOnly);
            }
        }
    }

    void EditorFluidVolumeComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        FluidVolumeComponent::GetProvidedServices(provided);
    }

    void EditorFluidVolumeComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        FluidVolumeComponent::GetIncompatibleServices(incompatible);
    }

    void EditorFluidVolumeComponent::GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required)
    {
        FluidVolumeComponent::GetRequiredServices(required);
    }

    void EditorFluidVolumeComponent::BuildGameEntity(AZ::Entity* gameEntity)
    {
        gameEntity->CreateComponent<FluidVolumeComponent>(m_settings);
    }
} // namespace FluidDynamics
