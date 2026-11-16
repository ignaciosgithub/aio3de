/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "WorldPartitionStreamingSourceComponent.h"

#include <AzCore/Component/TransformBus.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace WorldPartition
{
    void WorldPartitionStreamingSourceComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<WorldPartitionStreamingSourceComponent, AZ::Component>()
                ->Version(1)->Field("LoadRadius", &WorldPartitionStreamingSourceComponent::m_loadRadius);
            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<WorldPartitionStreamingSourceComponent>("World Partition Streaming Source", "Adds this entity as a streaming source")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")->Attribute(AZ::Edit::Attributes::Category, "World")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                    ->DataElement(AZ::Edit::UIHandlers::Default, &WorldPartitionStreamingSourceComponent::m_loadRadius,
                        "Load radius", "Zero uses the partition load radius.");
            }
        }
    }
    void WorldPartitionStreamingSourceComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("WorldPartitionStreamingSourceService"));
    }
    void WorldPartitionStreamingSourceComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("WorldPartitionStreamingSourceService"));
    }
    void WorldPartitionStreamingSourceComponent::Activate()
    {
        WorldPartitionStreamingSourceRequestBus::Handler::BusConnect();
    }
    void WorldPartitionStreamingSourceComponent::Deactivate()
    {
        WorldPartitionStreamingSourceRequestBus::Handler::BusDisconnect();
    }
    void WorldPartitionStreamingSourceComponent::GetStreamingSources(AZStd::vector<StreamingSource>& out)
    {
        AZ::Vector3 position = AZ::Vector3::CreateZero();
        AZ::TransformBus::EventResult(position, GetEntityId(), &AZ::TransformBus::Events::GetWorldTranslation);
        out.push_back({ position, m_loadRadius });
    }
}
