/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "EditorWindComponent.h"

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace FluidDynamics
{
    void EditorWindComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<EditorWindComponent, EditorComponentBase>()
                ->Version(1)
                ->Field("Settings", &EditorWindComponent::m_settings);

            if (auto* editContext = serializeContext->GetEditContext())
            {
                editContext->Class<EditorWindComponent>(
                    "Wind", "Procedural wind field (steady flow + gusts + turbulence) sampled by fluids and gameplay")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                        ->Attribute(AZ::Edit::Attributes::Category, "Physics")
                        ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                        ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EditorWindComponent::m_settings,
                        "Settings", "Wind field settings")
                        ->Attribute(AZ::Edit::Attributes::Visibility, AZ::Edit::PropertyVisibility::ShowChildrenOnly);
            }
        }
    }

    void EditorWindComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        WindComponent::GetProvidedServices(provided);
    }

    void EditorWindComponent::BuildGameEntity(AZ::Entity* gameEntity)
    {
        gameEntity->CreateComponent<WindComponent>(m_settings);
    }
} // namespace FluidDynamics
