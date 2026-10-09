/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "VoxelNavSystemComponent.h"

#include <AzCore/RTTI/BehaviorContext.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace VoxelNav
{
    void VoxelNavSystemComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<VoxelNavSystemComponent, AZ::Component>()->Version(1);

            if (AZ::EditContext* editContext = serializeContext->GetEditContext())
            {
                editContext->Class<VoxelNavSystemComponent>("Voxel Navigation", "Routes 3D voxel path queries to the Voxel Nav Volumes of the level")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                        ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("System"))
                        ->Attribute(AZ::Edit::Attributes::AutoExpand, true);
            }
        }

        if (auto* behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behaviorContext->EBus<VoxelNavRequestBus>("VoxelNavRequestBus")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Category, "VoxelNav")
                ->Attribute(AZ::Script::Attributes::Module, "voxelnav")
                ->Event("FindPath", &VoxelNavRequestBus::Events::FindPath)
                ->Event("FindRawPath", &VoxelNavRequestBus::Events::FindRawPath)
                ->Event("IsNavigable", &VoxelNavRequestBus::Events::IsNavigable)
                ->Event("GetNearestNavigable", &VoxelNavRequestBus::Events::GetNearestNavigable)
                ->Event("IsReady", &VoxelNavRequestBus::Events::IsReady)
                ->Event("Rebuild", &VoxelNavRequestBus::Events::Rebuild)
                ->Event("GetVolumeCount", &VoxelNavRequestBus::Events::GetVolumeCount)
                ->Event("FindVolumeAt", &VoxelNavRequestBus::Events::FindVolumeAt);
        }
    }

    void VoxelNavSystemComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("VoxelNavService"));
    }

    void VoxelNavSystemComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("VoxelNavService"));
    }

    void VoxelNavSystemComponent::Activate()
    {
        VoxelNavRequestBus::Handler::BusConnect();
    }

    void VoxelNavSystemComponent::Deactivate()
    {
        VoxelNavRequestBus::Handler::BusDisconnect();
    }

    VoxelNavVolumeRequests* VoxelNavSystemComponent::SelectVolume(const AZ::Vector3& position, const AZ::Vector3& fallback)
    {
        VoxelNavVolumeRequests* containingPosition = nullptr;
        VoxelNavVolumeRequests* containingFallback = nullptr;
        VoxelNavVolumeRequests* first = nullptr;
        VoxelNavVolumeRequestBus::EnumerateHandlers(
            [&](VoxelNavVolumeRequests* handler)
            {
                if (!first)
                {
                    first = handler;
                }
                if (!containingPosition && handler->ContainsPoint(position))
                {
                    containingPosition = handler;
                    return false;
                }
                if (!containingFallback && handler->ContainsPoint(fallback))
                {
                    containingFallback = handler;
                }
                return true;
            });
        if (containingPosition)
        {
            return containingPosition;
        }
        return containingFallback ? containingFallback : first;
    }

    AZStd::vector<AZ::Vector3> VoxelNavSystemComponent::FindPath(const AZ::Vector3& start, const AZ::Vector3& goal)
    {
        VoxelNavVolumeRequests* volume = SelectVolume(start, goal);
        return volume ? volume->FindPath(start, goal) : AZStd::vector<AZ::Vector3>{};
    }

    AZStd::vector<AZ::Vector3> VoxelNavSystemComponent::FindRawPath(const AZ::Vector3& start, const AZ::Vector3& goal)
    {
        VoxelNavVolumeRequests* volume = SelectVolume(start, goal);
        return volume ? volume->FindRawPath(start, goal) : AZStd::vector<AZ::Vector3>{};
    }

    bool VoxelNavSystemComponent::IsNavigable(const AZ::Vector3& position)
    {
        VoxelNavVolumeRequests* volume = SelectVolume(position, position);
        return volume && volume->IsNavigable(position);
    }

    AZ::Vector3 VoxelNavSystemComponent::GetNearestNavigable(const AZ::Vector3& position, float maxDistance)
    {
        VoxelNavVolumeRequests* volume = SelectVolume(position, position);
        return volume ? volume->GetNearestNavigable(position, maxDistance) : position;
    }

    bool VoxelNavSystemComponent::IsReady()
    {
        bool anyVolume = false;
        bool allReady = true;
        VoxelNavVolumeRequestBus::EnumerateHandlers(
            [&](VoxelNavVolumeRequests* handler)
            {
                anyVolume = true;
                allReady = allReady && handler->IsReady();
                return allReady;
            });
        return anyVolume && allReady;
    }

    void VoxelNavSystemComponent::Rebuild()
    {
        VoxelNavVolumeRequestBus::EnumerateHandlers(
            [](VoxelNavVolumeRequests* handler)
            {
                handler->Rebuild();
                return true;
            });
    }

    AZ::u32 VoxelNavSystemComponent::GetVolumeCount()
    {
        AZ::u32 count = 0;
        VoxelNavVolumeRequestBus::EnumerateHandlers(
            [&count]([[maybe_unused]] VoxelNavVolumeRequests* handler)
            {
                ++count;
                return true;
            });
        return count;
    }

    AZ::EntityId VoxelNavSystemComponent::FindVolumeAt(const AZ::Vector3& position)
    {
        AZ::EntityId result;
        VoxelNavVolumeRequestBus::EnumerateHandlers(
            [&](VoxelNavVolumeRequests* handler)
            {
                if (handler->ContainsPoint(position))
                {
                    result = handler->GetVolumeEntityId();
                    return false;
                }
                return true;
            });
        return result;
    }
} // namespace VoxelNav
