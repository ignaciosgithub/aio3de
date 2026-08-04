/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include "RemappableInputComponent.h"

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzFramework/Input/Channels/InputChannel.h>
#include <StartingPointInput/InputEventNotificationBus.h>

namespace GameSettings
{
    void RemappableBinding::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<RemappableBinding>()
                ->Version(1)
                ->Field("EventName", &RemappableBinding::m_eventName)
                ->Field("BindingKey", &RemappableBinding::m_bindingKey)
                ->Field("DefaultChannel", &RemappableBinding::m_defaultChannel)
                ->Field("ScaleSetting", &RemappableBinding::m_scaleSetting)
                ->Field("ScaleDefault", &RemappableBinding::m_scaleDefault);

            if (auto* editContext = serializeContext->GetEditContext())
            {
                editContext->Class<RemappableBinding>("Remappable Binding", "Raw input channel to input event mapping")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &RemappableBinding::m_eventName,
                        "Event Name", "StartingPointInput event to emit (e.g. Shoot)")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &RemappableBinding::m_bindingKey,
                        "Binding Key", "Settings key the user's rebind is stored under; empty = not rebindable")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &RemappableBinding::m_defaultChannel,
                        "Default Channel", "Input channel when no rebind is saved (e.g. mouse_button_left)")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &RemappableBinding::m_scaleSetting,
                        "Scale Setting", "Optional float setting that multiplies the event value (e.g. mouse_sensitivity)")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &RemappableBinding::m_scaleDefault,
                        "Scale Default", "Multiplier used while the scale setting is unset");
            }
        }
    }

    void RemappableInputComponent::Reflect(AZ::ReflectContext* context)
    {
        RemappableBinding::Reflect(context);

        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<RemappableInputComponent, AZ::Component>()
                ->Version(1)
                ->Field("Bindings", &RemappableInputComponent::m_bindings);

            if (auto* editContext = serializeContext->GetEditContext())
            {
                editContext->Class<RemappableInputComponent>("Remappable Input",
                    "Maps raw input channels to input events with live user rebinding and sensitivity scaling")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Category, "Gameplay")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                    ->DataElement(AZ::Edit::UIHandlers::Default, &RemappableInputComponent::m_bindings,
                        "Bindings", "Channel-to-event mappings");
            }
        }
    }

    void RemappableInputComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("RemappableInputService"));
    }

    void RemappableInputComponent::Activate()
    {
        RebuildChannelMap();
        GameSettingsNotificationBus::Handler::BusConnect();
        AzFramework::InputChannelEventListener::Connect();
    }

    void RemappableInputComponent::Deactivate()
    {
        AzFramework::InputChannelEventListener::Disconnect();
        GameSettingsNotificationBus::Handler::BusDisconnect();
    }

    void RemappableInputComponent::OnBindingChanged(
        [[maybe_unused]] const AZStd::string& bindingKey, [[maybe_unused]] const AZStd::string& channelName)
    {
        RebuildChannelMap();
    }

    void RemappableInputComponent::RebuildChannelMap()
    {
        m_channelToBindings.clear();
        for (const RemappableBinding& binding : m_bindings)
        {
            AZStd::string channel = binding.m_defaultChannel;
            if (!binding.m_bindingKey.empty())
            {
                GameSettingsRequestBus::BroadcastResult(
                    channel, &GameSettingsRequestBus::Events::GetBinding, binding.m_bindingKey, binding.m_defaultChannel);
            }
            if (!channel.empty())
            {
                m_channelToBindings[channel].push_back(&binding);
            }
        }
    }

    bool RemappableInputComponent::OnInputChannelEventFiltered(const AzFramework::InputChannel& inputChannel)
    {
        auto it = m_channelToBindings.find(inputChannel.GetInputChannelId().GetName());
        if (it == m_channelToBindings.end())
        {
            return false;
        }

        // while a rebind capture is pending, don't feed gameplay the press
        bool rebinding = false;
        GameSettingsRequestBus::BroadcastResult(rebinding, &GameSettingsRequestBus::Events::IsRebinding);
        if (rebinding)
        {
            return false;
        }

        const AzFramework::InputChannel::State state = inputChannel.GetState();
        for (const RemappableBinding* binding : it->second)
        {
            float scale = binding->m_scaleDefault;
            if (!binding->m_scaleSetting.empty())
            {
                GameSettingsRequestBus::BroadcastResult(
                    scale, &GameSettingsRequestBus::Events::GetValue, binding->m_scaleSetting, binding->m_scaleDefault);
            }
            const float value = inputChannel.GetValue() * scale;
            const StartingPointInput::InputEventNotificationId eventId(binding->m_eventName.c_str());
            switch (state)
            {
            case AzFramework::InputChannel::State::Began:
                StartingPointInput::InputEventNotificationBus::Event(
                    eventId, &StartingPointInput::InputEventNotificationBus::Events::OnPressed, value);
                break;
            case AzFramework::InputChannel::State::Updated:
                StartingPointInput::InputEventNotificationBus::Event(
                    eventId, &StartingPointInput::InputEventNotificationBus::Events::OnHeld, value);
                break;
            case AzFramework::InputChannel::State::Ended:
                StartingPointInput::InputEventNotificationBus::Event(
                    eventId, &StartingPointInput::InputEventNotificationBus::Events::OnReleased, 0.0f);
                break;
            default:
                break;
            }
        }
        return false; // never consume: other systems may also observe the channel
    }
} // namespace GameSettings
