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
#include <AzCore/std/parallel/condition_variable.h>
#include <AzCore/std/parallel/mutex.h>
#include <AzCore/std/parallel/thread.h>
#include <AzCore/std/string/string.h>

namespace ServerAdmin
{
    //! rcon-style remote administration channel.
    //!
    //! When `admin_enable` is true and `admin_password` is non-empty, a TCP
    //! listener accepts one admin session at a time. Authentication is an
    //! HMAC-SHA256 challenge-response over the shared password (the password
    //! itself never crosses the wire). Authenticated command lines execute
    //! on the main thread through the engine console; all console/log output
    //! produced during execution streams back, terminated by an `<<<END>>>`
    //! line. See scripts/rcon.py for the reference client.
    class ServerAdminSystemComponent
        : public AZ::Component
        , private AZ::TickBus::Handler
    {
    public:
        AZ_COMPONENT(ServerAdminSystemComponent, "{3F58A1D0-7C24-4B6E-9A83-D65F01B72E49}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);

        // AZ::Component
        void Activate() override;
        void Deactivate() override;

    private:
        // AZ::TickBus - executes queued admin commands on the main thread
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;

        void ListenerThread();
        void ServeClient(AZ::s64 clientSocket);
        void StartListenerIfConfigured();
        void StopListener();

        AZStd::thread m_listenerThread;
        AZStd::atomic_bool m_running{ false };
        AZ::s64 m_listenSocket = -1;

        // single in-flight command handed from the client thread to OnTick
        AZStd::mutex m_commandMutex;
        AZStd::condition_variable m_commandDone;
        AZStd::string m_pendingCommand;
        AZStd::string m_commandOutput;
        bool m_commandReady = false;
        bool m_commandFinished = false;

        float m_retryTimer = 0.0f;
    };
} // namespace ServerAdmin
