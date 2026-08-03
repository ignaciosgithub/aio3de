/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include "ArenaShooterNetSystemComponent.h"

#include <AzCore/Serialization/SerializeContext.h>
#include <Source/AutoGen/AutoComponentTypes.h>

namespace ArenaShooterNet
{
    void ArenaShooterNetSystemComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<ArenaShooterNetSystemComponent, AZ::Component>()
                ->Version(1);
        }
    }

    void ArenaShooterNetSystemComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("ArenaShooterNetService"));
    }

    void ArenaShooterNetSystemComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("ArenaShooterNetService"));
    }

    void ArenaShooterNetSystemComponent::GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required)
    {
        required.push_back(AZ_CRC_CE("MultiplayerService"));
    }

    void ArenaShooterNetSystemComponent::Activate()
    {
        RegisterMultiplayerComponents(); //< assigns NetComponentIds to this gem's multiplayer components
    }

    void ArenaShooterNetSystemComponent::Deactivate()
    {
    }
} // namespace ArenaShooterNet
