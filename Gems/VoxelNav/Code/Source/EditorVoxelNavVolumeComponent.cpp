/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "EditorVoxelNavVolumeComponent.h"

#include <AzCore/Component/TransformBus.h>
#include <AzCore/Math/Color.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

#include "VoxelNavVolumeComponent.h"

namespace VoxelNav
{
    void EditorVoxelNavVolumeComponent::Reflect(AZ::ReflectContext* context)
    {
        auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context);
        if (!serializeContext)
        {
            return;
        }

        serializeContext->Class<EditorVoxelNavVolumeComponent, EditorComponentBase>()->Version(1)->Field(
            "Settings", &EditorVoxelNavVolumeComponent::m_settings);

        AZ::EditContext* editContext = serializeContext->GetEditContext();
        if (!editContext)
        {
            return;
        }

        editContext->Class<EditorVoxelNavVolumeComponent>(
            "Voxel Nav Volume",
            "Bakes the physics colliders inside this volume into a 3D voxel grid for walking or flying pathfinding (VoxelNavRequestBus / C# Pathfinding)")
            ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                ->Attribute(AZ::Edit::Attributes::Category, "AI")
                ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
            ->DataElement(AZ::Edit::UIHandlers::Default, &EditorVoxelNavVolumeComponent::m_settings, "Settings", "Voxel nav volume settings")
                ->Attribute(AZ::Edit::Attributes::Visibility, AZ::Edit::PropertyVisibility::ShowChildrenOnly);
    }

    void EditorVoxelNavVolumeComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        VoxelNavVolumeComponent::GetProvidedServices(provided);
    }

    void EditorVoxelNavVolumeComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        VoxelNavVolumeComponent::GetIncompatibleServices(incompatible);
    }

    void EditorVoxelNavVolumeComponent::GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required)
    {
        VoxelNavVolumeComponent::GetRequiredServices(required);
    }

    void EditorVoxelNavVolumeComponent::Activate()
    {
        EditorComponentBase::Activate();
        AzFramework::EntityDebugDisplayEventBus::Handler::BusConnect(GetEntityId());
    }

    void EditorVoxelNavVolumeComponent::Deactivate()
    {
        AzFramework::EntityDebugDisplayEventBus::Handler::BusDisconnect();
        EditorComponentBase::Deactivate();
    }

    void EditorVoxelNavVolumeComponent::BuildGameEntity(AZ::Entity* gameEntity)
    {
        gameEntity->CreateComponent<VoxelNavVolumeComponent>(m_settings);
    }

    void EditorVoxelNavVolumeComponent::DisplayEntityViewport(
        [[maybe_unused]] const AzFramework::ViewportInfo& viewportInfo, AzFramework::DebugDisplayRequests& debugDisplay)
    {
        AZ::Vector3 center = AZ::Vector3::CreateZero();
        AZ::TransformBus::EventResult(center, GetEntityId(), &AZ::TransformBus::Events::GetWorldTranslation);
        const AZ::Vector3 halfSize = m_settings.m_size.GetMax(AZ::Vector3(0.0f)) * 0.5f;
        debugDisplay.SetColor(AZ::Color(0.2f, 0.8f, 1.0f, 1.0f));
        debugDisplay.DrawWireBox(center - halfSize, center + halfSize);
    }
} // namespace VoxelNav
