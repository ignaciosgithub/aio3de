/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include "GameSettingsSystemComponent.h"

#include <AzCore/IO/FileIO.h>
#include <AzCore/JSON/document.h>
#include <AzCore/JSON/prettywriter.h>
#include <AzCore/JSON/stringbuffer.h>
#include <AzCore/RTTI/BehaviorContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzFramework/Input/Channels/InputChannel.h>
#include <AzFramework/Input/Devices/InputDevice.h>

namespace GameSettings
{
    //! Script (Lua/Script Canvas) handler for GameSettingsNotificationBus.
    class BehaviorGameSettingsNotificationBusHandler
        : public GameSettingsNotificationBus::Handler
        , public AZ::BehaviorEBusHandler
    {
    public:
        AZ_EBUS_BEHAVIOR_BINDER(BehaviorGameSettingsNotificationBusHandler, "{4B7E36D9-A150-4C88-9F02-6E3D51B87C40}", AZ::SystemAllocator,
            OnSettingChanged, OnBindingChanged);

        void OnSettingChanged(const AZStd::string& name, float value) override
        {
            Call(FN_OnSettingChanged, name, value);
        }

        void OnBindingChanged(const AZStd::string& bindingKey, const AZStd::string& channelName) override
        {
            Call(FN_OnBindingChanged, bindingKey, channelName);
        }
    };

    namespace
    {
        constexpr const char* SettingsPath = "@user@/gamesettings.json";

        //! Only discrete key/button/trigger channels are valid rebind targets;
        //! movement axes (mouse deltas, thumbsticks) would bind on the first twitch.
        bool IsBindableChannel(const AzFramework::InputChannel& channel)
        {
            const AZStd::string_view name(channel.GetInputChannelId().GetName());
            return name.starts_with("keyboard_key_") ||
                name.starts_with("mouse_button_") ||
                name.starts_with("gamepad_button_") ||
                name.starts_with("gamepad_trigger_");
        }
    } // namespace

    void GameSettingsSystemComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<GameSettingsSystemComponent, AZ::Component>()->Version(1);
        }

        if (auto* behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behaviorContext->EBus<GameSettingsRequestBus>("GameSettingsRequestBus")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Category, "Gameplay")
                ->Event("GetValue", &GameSettingsRequestBus::Events::GetValue)
                ->Event("SetValue", &GameSettingsRequestBus::Events::SetValue)
                ->Event("GetBinding", &GameSettingsRequestBus::Events::GetBinding)
                ->Event("SetBinding", &GameSettingsRequestBus::Events::SetBinding)
                ->Event("StartRebind", &GameSettingsRequestBus::Events::StartRebind)
                ->Event("IsRebinding", &GameSettingsRequestBus::Events::IsRebinding)
                ->Event("Save", &GameSettingsRequestBus::Events::Save);

            behaviorContext->EBus<GameSettingsNotificationBus>("GameSettingsNotificationBus")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Category, "Gameplay")
                ->Handler<BehaviorGameSettingsNotificationBusHandler>();
        }
    }

    void GameSettingsSystemComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("GameSettingsService"));
    }

    void GameSettingsSystemComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("GameSettingsService"));
    }

    void GameSettingsSystemComponent::Activate()
    {
        Load();
        GameSettingsRequestBus::Handler::BusConnect();
        AzFramework::InputChannelEventListener::Connect();
    }

    void GameSettingsSystemComponent::Deactivate()
    {
        AzFramework::InputChannelEventListener::Disconnect();
        GameSettingsRequestBus::Handler::BusDisconnect();
        if (m_dirty)
        {
            Save();
        }
    }

    float GameSettingsSystemComponent::GetValue(const AZStd::string& name, float defaultValue)
    {
        auto it = m_values.find(name);
        return it != m_values.end() ? it->second : defaultValue;
    }

    void GameSettingsSystemComponent::SetValue(const AZStd::string& name, float value)
    {
        m_values[name] = value;
        m_dirty = true;
        GameSettingsNotificationBus::Broadcast(&GameSettingsNotificationBus::Events::OnSettingChanged, name, value);
    }

    AZStd::string GameSettingsSystemComponent::GetBinding(const AZStd::string& bindingKey, const AZStd::string& defaultChannel)
    {
        auto it = m_bindings.find(bindingKey);
        return it != m_bindings.end() ? it->second : defaultChannel;
    }

    void GameSettingsSystemComponent::SetBinding(const AZStd::string& bindingKey, const AZStd::string& channelName)
    {
        m_bindings[bindingKey] = channelName;
        m_dirty = true;
        GameSettingsNotificationBus::Broadcast(&GameSettingsNotificationBus::Events::OnBindingChanged, bindingKey, channelName);
    }

    void GameSettingsSystemComponent::StartRebind(const AZStd::string& bindingKey)
    {
        m_rebindKey = bindingKey;
    }

    bool GameSettingsSystemComponent::IsRebinding()
    {
        return !m_rebindKey.empty();
    }

    bool GameSettingsSystemComponent::OnInputChannelEventFiltered(const AzFramework::InputChannel& inputChannel)
    {
        if (m_rebindKey.empty())
        {
            return false;
        }
        if (inputChannel.GetState() != AzFramework::InputChannel::State::Began || !IsBindableChannel(inputChannel))
        {
            return false;
        }
        const AZStd::string key = m_rebindKey;
        m_rebindKey.clear();
        SetBinding(key, inputChannel.GetInputChannelId().GetName());
        return true; // consume the capturing press
    }

    AZ::s32 GameSettingsSystemComponent::GetPriority() const
    {
        // above default listeners so a capture consumes the press before gameplay sees it
        return AzFramework::InputChannelEventListener::GetPriorityUI() + 1;
    }

    void GameSettingsSystemComponent::Load()
    {
        auto* fileIo = AZ::IO::FileIOBase::GetInstance();
        if (!fileIo)
        {
            return;
        }
        AZ::IO::HandleType handle = AZ::IO::InvalidHandle;
        if (!fileIo->Open(SettingsPath, AZ::IO::OpenMode::ModeRead | AZ::IO::OpenMode::ModeText, handle))
        {
            return; // first run: nothing saved yet
        }
        AZ::u64 size = 0;
        fileIo->Size(handle, size);
        AZStd::string content(size, '\0');
        fileIo->Read(handle, content.data(), size);
        fileIo->Close(handle);

        rapidjson::Document doc;
        doc.Parse(content.c_str());
        if (doc.HasParseError() || !doc.IsObject())
        {
            AZ_Warning("GameSettings", false, "Failed to parse %s; using defaults", SettingsPath);
            return;
        }
        if (auto values = doc.FindMember("values"); values != doc.MemberEnd() && values->value.IsObject())
        {
            for (auto it = values->value.MemberBegin(); it != values->value.MemberEnd(); ++it)
            {
                if (it->value.IsNumber())
                {
                    m_values[it->name.GetString()] = it->value.GetFloat();
                }
            }
        }
        if (auto bindings = doc.FindMember("bindings"); bindings != doc.MemberEnd() && bindings->value.IsObject())
        {
            for (auto it = bindings->value.MemberBegin(); it != bindings->value.MemberEnd(); ++it)
            {
                if (it->value.IsString())
                {
                    m_bindings[it->name.GetString()] = it->value.GetString();
                }
            }
        }
    }

    void GameSettingsSystemComponent::Save()
    {
        auto* fileIo = AZ::IO::FileIOBase::GetInstance();
        if (!fileIo)
        {
            return;
        }

        rapidjson::Document doc(rapidjson::kObjectType);
        auto& alloc = doc.GetAllocator();
        rapidjson::Value values(rapidjson::kObjectType);
        for (const auto& [name, value] : m_values)
        {
            values.AddMember(rapidjson::Value(name.c_str(), alloc), rapidjson::Value(value), alloc);
        }
        doc.AddMember("values", values, alloc);
        rapidjson::Value bindings(rapidjson::kObjectType);
        for (const auto& [key, channel] : m_bindings)
        {
            bindings.AddMember(rapidjson::Value(key.c_str(), alloc), rapidjson::Value(channel.c_str(), alloc), alloc);
        }
        doc.AddMember("bindings", bindings, alloc);

        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        doc.Accept(writer);

        AZ::IO::HandleType handle = AZ::IO::InvalidHandle;
        if (fileIo->Open(SettingsPath, AZ::IO::OpenMode::ModeWrite | AZ::IO::OpenMode::ModeText, handle))
        {
            fileIo->Write(handle, buffer.GetString(), buffer.GetSize());
            fileIo->Close(handle);
            m_dirty = false;
        }
        else
        {
            AZ_Warning("GameSettings", false, "Could not write %s", SettingsPath);
        }
    }
} // namespace GameSettings
