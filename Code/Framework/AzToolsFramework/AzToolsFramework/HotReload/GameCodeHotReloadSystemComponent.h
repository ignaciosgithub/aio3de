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
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>
#include <AzToolsFramework/ActionManager/ActionManagerRegistrationNotificationBus.h>
#include <AzToolsFramework/AzToolsFrameworkAPI.h>

namespace AzToolsFramework
{
    class AZTF_API GameCodeHotReloadSystemComponent
        : public AZ::Component
        , private AZ::TickBus::Handler
        , private ActionManagerRegistrationNotificationBus::Handler
    {
    public:
        AZ_COMPONENT(GameCodeHotReloadSystemComponent, "{A47EF355-5EC2-4F53-97D8-A2F7D958105B}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);

        void Activate() override;
        void Deactivate() override;

        void ReloadGameCode();

    private:
        struct WatchedModule
        {
            AZ::u64 m_modificationTime = 0;
            float m_secondsWithoutChanges = 0.0f;
            bool m_changed = false;
            bool m_initialized = false;
        };

        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;
        void OnActionRegistrationHook() override;
        void OnMenuBindingHook() override;

        void QueueReload(AZStd::vector<AZStd::string> modulePaths);
        void PollModuleFiles(float deltaTime);
        AZStd::vector<AZStd::string> GetConfiguredModuleNames() const;
        AZStd::vector<AZStd::string> GetLoadedConfiguredModulePaths() const;
        void PerformReload(const AZStd::vector<AZStd::string>& modulePaths);

        bool m_reloadRequested = false;
        bool m_waitingForPlayModeStop = false;
        float m_watchPollElapsed = 0.0f;
        AZStd::vector<AZStd::string> m_requestedModulePaths;
        AZStd::unordered_map<AZStd::string, WatchedModule> m_watchedModules;
    };
} // namespace AzToolsFramework
