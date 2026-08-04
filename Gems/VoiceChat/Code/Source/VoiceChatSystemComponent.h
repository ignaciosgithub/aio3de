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
#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>

#include <AzNetworking/UdpTransport/UdpSocket.h>
#include <AzNetworking/Utilities/IpAddress.h>

#include <VoiceChat/VoiceChatBus.h>

#include <miniaudio.h>

namespace VoiceChat
{
    //! Client voice capture/playback and (with voice_host true) the
    //! dedicated-server relay that routes voice to same-channel clients.
    class VoiceChatSystemComponent
        : public AZ::Component
        , public VoiceChatRequestBus::Handler
        , private AZ::TickBus::Handler
    {
    public:
        AZ_COMPONENT(VoiceChatSystemComponent, "{20D5873E-6B41-4F9A-BC08-71E3D2A6F095}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);

        // AZ::Component
        void Activate() override;
        void Deactivate() override;

        // VoiceChatRequestBus
        void ConnectVoice(const AZStd::string& address, AZ::u32 port, AZ::u32 channel) override;
        void DisconnectVoice() override;
        void SetChannel(AZ::u32 channel) override;
        void SetTalking(bool talking) override;
        bool IsTalking() override;
        void SetMuted(bool muted) override;
        bool IsMuted() override;
        void SetVoiceVolume(float volume) override;

    private:
        // AZ::TickBus
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;

        void TickClient(float deltaTime);
        void TickServer();
        void StartServerIfConfigured();
        void SendJoin();
        void CaptureAndSend(float deltaTime);
        void PlayIncoming(AZ::u32 talkerId, const AZ::u8* payload, size_t size);

        struct Talker
        {
            ma_pcm_rb* m_ringBuffer = nullptr;
            ma_sound* m_sound = nullptr;
            float m_silence = 0.0f;
            bool m_active = false;
        };
        void DestroyTalker(Talker& talker);

        void SendPacket(AzNetworking::UdpSocket& socket, const AzNetworking::IpAddress& to, const AZ::u8* data, AZ::u32 size);

        // client state
        AZStd::unique_ptr<AzNetworking::UdpSocket> m_clientSocket;
        AzNetworking::IpAddress m_serverAddress;
        AZ::u8 m_channel = 0;
        bool m_connected = false;
        bool m_talking = false;
        bool m_muted = false;
        float m_volume = 1.0f;
        float m_keepaliveTimer = 0.0f;
        bool m_micSessionActive = false;
        AZ::u16 m_sendSequence = 0;
        AZStd::vector<AZ::s16> m_pcmScratch;
        AZStd::unordered_map<AZ::u32, Talker> m_talkers;

        // server (relay) state
        struct RelayClient
        {
            AzNetworking::IpAddress m_address;
            AZ::u8 m_channel = 0;
            AZ::u16 m_talkerId = 0;
            float m_lastSeen = 0.0f;
        };
        AZStd::unique_ptr<AzNetworking::UdpSocket> m_serverSocket;
        AZStd::vector<RelayClient> m_relayClients;
        AZ::u16 m_nextTalkerId = 1;
        float m_serverTime = 0.0f;
    };
} // namespace VoiceChat
