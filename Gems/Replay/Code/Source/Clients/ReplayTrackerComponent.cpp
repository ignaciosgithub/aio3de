/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "ReplayTrackerComponent.h"

#include <AzCore/Component/Entity.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

#include <Replay/ReplayBus.h>

namespace Replay
{
    void ReplayTrackerConfig::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<ReplayTrackerConfig>()
                ->Version(1)
                ->Field("TrackName", &ReplayTrackerConfig::m_trackName)
                ->Field("SampleRateFps", &ReplayTrackerConfig::m_sampleRateFps);

            if (auto* editContext = serializeContext->GetEditContext())
            {
                editContext->Class<ReplayTrackerConfig>("Replay Tracker settings", "")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ReplayTrackerConfig::m_trackName,
                        "Track name", "Name used to match this entity during playback. Empty uses the entity name.")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ReplayTrackerConfig::m_sampleRateFps,
                        "Sample rate", "Transform samples recorded per second. 0 records every frame.")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Suffix, " fps");
            }
        }
    }

    ReplayTrackerComponent::ReplayTrackerComponent(const ReplayTrackerConfig& config)
        : m_config(config)
    {
    }

    void ReplayTrackerComponent::Reflect(AZ::ReflectContext* context)
    {
        ReplayTrackerConfig::Reflect(context);

        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<ReplayTrackerComponent, AZ::Component>()
                ->Version(1)
                ->Field("Config", &ReplayTrackerComponent::m_config);
        }
    }

    void ReplayTrackerComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("ReplayTrackerService"));
    }

    void ReplayTrackerComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("ReplayTrackerService"));
    }

    void ReplayTrackerComponent::Activate()
    {
        AZStd::string trackName = m_config.m_trackName;
        if (trackName.empty() && GetEntity())
        {
            trackName = GetEntity()->GetName();
        }

        ReplayTrackerRegistrationBus::Broadcast(
            &ReplayTrackerRegistrations::RegisterTracker, GetEntityId(), trackName, m_config.m_sampleRateFps);
    }

    void ReplayTrackerComponent::Deactivate()
    {
        ReplayTrackerRegistrationBus::Broadcast(&ReplayTrackerRegistrations::UnregisterTracker, GetEntityId());
    }
} // namespace Replay
