/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/std/parallel/atomic.h>
#include <AzCore/std/parallel/thread.h>
#include <AzCore/std/string/string.h>

#include <ServerBrowser/ServerBrowserBus.h>

namespace ServerBrowser
{
    //! Master-server client (list refresh + join) and dedicated-server
    //! announcer, speaking plain HTTP to scripts/master_server.py.
    class ServerBrowserSystemComponent
        : public AZ::Component
        , public ServerBrowserRequestBus::Handler
        , private AZ::TickBus::Handler
    {
    public:
        AZ_COMPONENT(ServerBrowserSystemComponent, "{D07A54E2-63B9-4C18-8FA5-2E91D6B30C74}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);

        // AZ::Component
        void Activate() override;
        void Deactivate() override;

        // ServerBrowserRequestBus
        void RefreshServerList() override;
        void JoinServer(const AZStd::string& address, AZ::u32 port) override;

    private:
        // AZ::TickBus (server announce heartbeat)
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;

        void Announce();

        AZStd::thread m_worker;
        AZStd::atomic_bool m_busy{ false };
        float m_announceTimer = 0.0f;
    };
} // namespace ServerBrowser
