/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzCore/Component/Component.h>

namespace ArenaShooterNet
{
    //! Registers the gem's multiplayer components with the Multiplayer gem's
    //! component registry so they get stable NetComponentIds.
    class ArenaShooterNetSystemComponent
        : public AZ::Component
    {
    public:
        AZ_COMPONENT(ArenaShooterNetSystemComponent, "{0E4B6A9D-7C51-4E2F-B3A8-9F60D2C81E74}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required);

    protected:
        void Activate() override;
        void Deactivate() override;
    };
} // namespace ArenaShooterNet
