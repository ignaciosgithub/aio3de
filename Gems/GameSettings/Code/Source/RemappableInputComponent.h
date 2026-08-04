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
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>
#include <AzFramework/Input/Events/InputChannelEventListener.h>

#include <GameSettings/GameSettingsBus.h>

namespace GameSettings
{
    //! One rebindable mapping from a raw input channel to a named input event.
    struct RemappableBinding
    {
        AZ_TYPE_INFO(RemappableBinding, "{7D2E94B1-06AF-4C53-8E27-B934F1A6D085}");
        static void Reflect(AZ::ReflectContext* context);

        AZStd::string m_eventName;       //!< StartingPointInput event to emit (e.g. "Shoot")
        AZStd::string m_bindingKey;      //!< settings key the user override is stored under (e.g. "shoot_kbm"); empty = not rebindable
        AZStd::string m_defaultChannel;  //!< input channel used when no override is saved (e.g. "mouse_button_left")
        AZStd::string m_scaleSetting;    //!< optional float setting multiplying the event value (e.g. "mouse_sensitivity")
        float m_scaleDefault = 1.0f;     //!< scale used while the setting is unset
    };

    //! Runtime-rebindable replacement for the Input component: maps raw input
    //! channels to StartingPointInput events, resolving each mapping's channel
    //! through the GameSettings binding store (so StartRebind takes effect
    //! live) and scaling values by named settings (sensitivity).
    class RemappableInputComponent
        : public AZ::Component
        , private AzFramework::InputChannelEventListener
        , private GameSettingsNotificationBus::Handler
    {
    public:
        AZ_COMPONENT(RemappableInputComponent, "{B85F30C2-91E4-4D07-A6B8-F25D30C7194A}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);

        // AZ::Component
        void Activate() override;
        void Deactivate() override;

    private:
        // AzFramework::InputChannelEventListener
        bool OnInputChannelEventFiltered(const AzFramework::InputChannel& inputChannel) override;

        // GameSettingsNotificationBus
        void OnBindingChanged(const AZStd::string& bindingKey, const AZStd::string& channelName) override;

        void RebuildChannelMap();

        AZStd::vector<RemappableBinding> m_bindings;
        AZStd::unordered_map<AZStd::string, AZStd::vector<const RemappableBinding*>> m_channelToBindings;
    };
} // namespace GameSettings
