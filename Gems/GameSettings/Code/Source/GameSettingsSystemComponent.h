/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/string/string.h>
#include <AzFramework/Input/Events/InputChannelEventListener.h>

#include <GameSettings/GameSettingsBus.h>

namespace GameSettings
{
    //! Persistent per-user settings store (floats + input-binding overrides)
    //! with a next-key rebind capture, saved as JSON in the user folder.
    class GameSettingsSystemComponent
        : public AZ::Component
        , public GameSettingsRequestBus::Handler
        , private AzFramework::InputChannelEventListener
    {
    public:
        AZ_COMPONENT(GameSettingsSystemComponent, "{9C1B54D7-3E82-4A6F-B049-5D17C8A2F634}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);

        // AZ::Component
        void Activate() override;
        void Deactivate() override;

        // GameSettingsRequestBus
        float GetValue(const AZStd::string& name, float defaultValue) override;
        void SetValue(const AZStd::string& name, float value) override;
        AZStd::string GetBinding(const AZStd::string& bindingKey, const AZStd::string& defaultChannel) override;
        void SetBinding(const AZStd::string& bindingKey, const AZStd::string& channelName) override;
        void StartRebind(const AZStd::string& bindingKey) override;
        bool IsRebinding() override;
        void Save() override;

    private:
        // AzFramework::InputChannelEventListener
        bool OnInputChannelEventFiltered(const AzFramework::InputChannel& inputChannel) override;
        AZ::s32 GetPriority() const override;

        void Load();
        AZStd::string SettingsFilePath() const;

        AZStd::unordered_map<AZStd::string, float> m_values;
        AZStd::unordered_map<AZStd::string, AZStd::string> m_bindings;
        AZStd::string m_rebindKey; // non-empty while capturing
        bool m_dirty = false;
    };
} // namespace GameSettings
