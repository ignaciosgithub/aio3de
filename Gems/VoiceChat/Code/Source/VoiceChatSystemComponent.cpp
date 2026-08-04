/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include "VoiceChatSystemComponent.h"

#include <AzCore/Casting/numeric_cast.h>
#include <AzCore/Console/IConsole.h>
#include <AzCore/Console/ILogger.h>
#include <AzCore/Math/MathUtils.h>
#include <AzCore/RTTI/BehaviorContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/algorithm.h>

#include <AzNetworking/ConnectionLayer/IConnection.h>
#include <AzNetworking/UdpTransport/DtlsEndpoint.h>

#include <cmath>
#include <cstring>

#include <MicrophoneBus.h>
#include <MiniAudio/MiniAudioBus.h>
#include <miniaudio.h>

#include "MuLaw.h"

namespace VoiceChat
{
    AZ_CVAR(bool, voice_host, false, nullptr, AZ::ConsoleFunctorFlags::DontReplicate,
        "Host the voice relay on this (dedicated server) process");
    AZ_CVAR(uint16_t, voice_port, 33452, nullptr, AZ::ConsoleFunctorFlags::DontReplicate,
        "UDP port for the voice relay");
    AZ_CVAR(float, voice_vad_threshold, 0.0f, nullptr, AZ::ConsoleFunctorFlags::DontReplicate,
        "Voice activation RMS threshold while talking (0 = send everything)");

    namespace
    {
        constexpr AZ::u32 Magic = 0x31304356; // "VC01" little-endian
        constexpr AZ::u8 TypeJoin = 1;
        constexpr AZ::u8 TypeVoiceToServer = 2;
        constexpr AZ::u8 TypeVoiceToClient = 3;
        constexpr AZ::u8 TypeLeave = 4;

        constexpr AZ::u32 SampleRate = 16000;
        constexpr size_t FrameSamples = 320;           // 20 ms at 16 kHz
        constexpr size_t MaxDatagram = 1400;
        constexpr float ClientTimeout = 15.0f;         // relay drops silent clients
        constexpr float KeepaliveInterval = 5.0f;
        constexpr float TalkerSilenceTimeout = 0.5f;   // HUD "talking" indicator decay
        constexpr AZ::u32 RingBufferFrames = SampleRate; // 1 s of buffered audio per talker
        constexpr size_t MaxRelayClients = 64;
        constexpr size_t MaxTalkers = 32;
        constexpr size_t VoiceToServerHeader = 7;        // magic + type + sequence
        constexpr size_t VoiceToClientHeader = 9;        // magic + type + talker id + sequence
        constexpr size_t MaxVoiceToServer = VoiceToServerHeader + FrameSamples;
        constexpr size_t MaxVoiceToClient = VoiceToClientHeader + FrameSamples;
        static_assert(MaxVoiceToClient <= MaxDatagram, "a relayed voice packet must fit the datagram buffer");

        void WriteU16(AZ::u8* dest, AZ::u16 value)
        {
            dest[0] = static_cast<AZ::u8>(value & 0xFF);
            dest[1] = static_cast<AZ::u8>(value >> 8);
        }

        AZ::u16 ReadU16(const AZ::u8* src)
        {
            return static_cast<AZ::u16>(src[0]) | (static_cast<AZ::u16>(src[1]) << 8);
        }

        void WriteU32(AZ::u8* dest, AZ::u32 value)
        {
            for (int i = 0; i < 4; ++i)
            {
                dest[i] = static_cast<AZ::u8>((value >> (8 * i)) & 0xFF);
            }
        }

        AZ::u32 ReadU32(const AZ::u8* src)
        {
            AZ::u32 value = 0;
            for (int i = 0; i < 4; ++i)
            {
                value |= static_cast<AZ::u32>(src[i]) << (8 * i);
            }
            return value;
        }

    } // namespace

    //! Script (Lua/Script Canvas) handler for VoiceChatNotificationBus.
    class BehaviorVoiceChatNotificationBusHandler
        : public VoiceChatNotificationBus::Handler
        , public AZ::BehaviorEBusHandler
    {
    public:
        AZ_EBUS_BEHAVIOR_BINDER(BehaviorVoiceChatNotificationBusHandler, "{E4A0925D-17C8-4B6F-93D2-508CA1B7F3E6}", AZ::SystemAllocator,
            OnTalkerActive);

        void OnTalkerActive(AZ::u32 talkerId, bool active) override
        {
            Call(FN_OnTalkerActive, talkerId, active);
        }
    };

    void VoiceChatSystemComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<VoiceChatSystemComponent, AZ::Component>()->Version(1);
        }

        if (auto* behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behaviorContext->EBus<VoiceChatRequestBus>("VoiceChatRequestBus")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Category, "Multiplayer")
                ->Event("ConnectVoice", &VoiceChatRequestBus::Events::ConnectVoice)
                ->Event("DisconnectVoice", &VoiceChatRequestBus::Events::DisconnectVoice)
                ->Event("SetChannel", &VoiceChatRequestBus::Events::SetChannel)
                ->Event("SetTalking", &VoiceChatRequestBus::Events::SetTalking)
                ->Event("IsTalking", &VoiceChatRequestBus::Events::IsTalking)
                ->Event("SetMuted", &VoiceChatRequestBus::Events::SetMuted)
                ->Event("IsMuted", &VoiceChatRequestBus::Events::IsMuted)
                ->Event("SetVoiceVolume", &VoiceChatRequestBus::Events::SetVoiceVolume);

            behaviorContext->EBus<VoiceChatNotificationBus>("VoiceChatNotificationBus")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Category, "Multiplayer")
                ->Handler<BehaviorVoiceChatNotificationBusHandler>();
        }
    }

    void VoiceChatSystemComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("VoiceChatService"));
    }

    void VoiceChatSystemComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("VoiceChatService"));
    }

    void VoiceChatSystemComponent::Activate()
    {
        VoiceChatRequestBus::Handler::BusConnect();
        AZ::TickBus::Handler::BusConnect();
    }

    void VoiceChatSystemComponent::Deactivate()
    {
        DisconnectVoice();
        if (m_serverSocket)
        {
            m_serverSocket->Close();
            m_serverSocket.reset();
        }
        AZ::TickBus::Handler::BusDisconnect();
        VoiceChatRequestBus::Handler::BusDisconnect();
    }

    void VoiceChatSystemComponent::SendPacket(
        AzNetworking::UdpSocket& socket, const AzNetworking::IpAddress& to, const AZ::u8* data, AZ::u32 size)
    {
        AzNetworking::DtlsEndpoint noEncryption;
        const AzNetworking::ConnectionQuality quality;
        const int32_t sent = socket.Send(to, data, size, false, noEncryption, quality);
        if (sent < 0)
        {
            AZLOG_WARN("VoiceChat: failed to send %u bytes to %s", size, to.GetString().c_str());
        }
    }

    void VoiceChatSystemComponent::ConnectVoice(const AZStd::string& address, AZ::u32 port, AZ::u32 channel)
    {
        DisconnectVoice();
        AZLOG_INFO("VoiceChat: connecting to voice relay at %s:%u (channel %u)", address.c_str(), port, channel);
        m_serverAddress = AzNetworking::IpAddress(
            address.c_str(), aznumeric_cast<uint16_t>(port), AzNetworking::ProtocolType::Udp);
        m_clientSocket = AZStd::make_unique<AzNetworking::UdpSocket>();
        if (!m_serverAddress.IsValid() ||
            !m_clientSocket->Open(0, AzNetworking::UdpSocket::CanAcceptConnections::False, AzNetworking::TrustZone::ExternalClientToServer))
        {
            AZLOG_WARN("VoiceChat: could not reach voice relay at %s:%u", address.c_str(), port);
            m_clientSocket.reset();
            return;
        }
        m_channel = aznumeric_cast<AZ::u8>(channel);
        m_connected = true;
        m_keepaliveTimer = 0.0f;
        SendJoin();
    }

    void VoiceChatSystemComponent::DisconnectVoice()
    {
        if (m_connected && m_clientSocket)
        {
            AZ::u8 packet[5];
            WriteU32(packet, Magic);
            packet[4] = TypeLeave;
            SendPacket(*m_clientSocket, m_serverAddress, packet, sizeof(packet));
        }
        if (m_clientSocket)
        {
            m_clientSocket->Close();
            m_clientSocket.reset();
        }
        if (m_micSessionActive)
        {
            Audio::MicrophoneRequestBus::Broadcast(&Audio::MicrophoneRequestBus::Events::EndSession);
            Audio::MicrophoneRequestBus::Broadcast(&Audio::MicrophoneRequestBus::Events::ShutdownDevice);
            m_micSessionActive = false;
        }
        for (auto& [talkerId, talker] : m_talkers)
        {
            DestroyTalker(talker);
        }
        m_talkers.clear();
        m_connected = false;
        m_talking = false;
    }

    void VoiceChatSystemComponent::SetChannel(AZ::u32 channel)
    {
        m_channel = aznumeric_cast<AZ::u8>(channel);
        if (m_connected)
        {
            SendJoin();
        }
    }

    void VoiceChatSystemComponent::SetTalking(bool talking)
    {
        if (talking == m_talking)
        {
            return;
        }
        m_talking = talking;
        if (talking && m_connected && !m_micSessionActive)
        {
            bool initialized = false;
            Audio::MicrophoneRequestBus::BroadcastResult(
                initialized, &Audio::MicrophoneRequestBus::Events::InitializeDevice);
            bool started = false;
            if (initialized)
            {
                Audio::MicrophoneRequestBus::BroadcastResult(
                    started, &Audio::MicrophoneRequestBus::Events::StartSession);
            }
            m_micSessionActive = started;
            if (!started)
            {
                AZLOG_WARN("VoiceChat: no microphone available (is the Microphone gem enabled on this platform?)");
            }
        }
        else if (!talking && m_micSessionActive)
        {
            Audio::MicrophoneRequestBus::Broadcast(&Audio::MicrophoneRequestBus::Events::EndSession);
            Audio::MicrophoneRequestBus::Broadcast(&Audio::MicrophoneRequestBus::Events::ShutdownDevice);
            m_micSessionActive = false;
        }
    }

    bool VoiceChatSystemComponent::IsTalking()
    {
        return m_talking;
    }

    void VoiceChatSystemComponent::SetMuted(bool muted)
    {
        m_muted = muted;
    }

    bool VoiceChatSystemComponent::IsMuted()
    {
        return m_muted;
    }

    void VoiceChatSystemComponent::SetVoiceVolume(float volume)
    {
        m_volume = AZStd::clamp(volume, 0.0f, 4.0f);
        for (auto& [talkerId, talker] : m_talkers)
        {
            if (talker.m_sound)
            {
                ma_sound_set_volume(talker.m_sound, m_volume);
            }
        }
    }

    void VoiceChatSystemComponent::SendJoin()
    {
        AZ::u8 packet[6];
        WriteU32(packet, Magic);
        packet[4] = TypeJoin;
        packet[5] = m_channel;
        SendPacket(*m_clientSocket, m_serverAddress, packet, sizeof(packet));
    }

    void VoiceChatSystemComponent::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        StartServerIfConfigured();
        if (m_serverSocket)
        {
            m_serverTime += deltaTime;
            TickServer();
        }
        if (m_connected)
        {
            TickClient(deltaTime);
        }
    }

    void VoiceChatSystemComponent::StartServerIfConfigured()
    {
        if (m_serverSocket || !static_cast<bool>(voice_host))
        {
            return;
        }
        m_serverSocket = AZStd::make_unique<AzNetworking::UdpSocket>();
        if (!m_serverSocket->Open(
                static_cast<uint16_t>(voice_port),
                AzNetworking::UdpSocket::CanAcceptConnections::False,
                AzNetworking::TrustZone::ExternalClientToServer))
        {
            AZLOG_ERROR("VoiceChat: failed to bind voice relay on UDP %u", static_cast<AZ::u32>(static_cast<uint16_t>(voice_port)));
            m_serverSocket.reset();
            return;
        }
        AZLOG_INFO("VoiceChat: voice relay listening on UDP %u", static_cast<AZ::u32>(static_cast<uint16_t>(voice_port)));
    }

    void VoiceChatSystemComponent::TickServer()
    {
        AZ::u8 buffer[MaxDatagram];
        for (;;)
        {
            AzNetworking::IpAddress from;
            const int32_t received = m_serverSocket->Receive(from, buffer, sizeof(buffer));
            if (received <= 0)
            {
                break;
            }
            if (received < 5 || ReadU32(buffer) != Magic)
            {
                continue;
            }
            const AZ::u8 type = buffer[4];

            auto client = AZStd::find_if(
                m_relayClients.begin(), m_relayClients.end(),
                [&from](const RelayClient& existing) { return existing.m_address == from; });

            if (type == TypeJoin && received >= 6)
            {
                if (client == m_relayClients.end())
                {
                    if (m_relayClients.size() >= MaxRelayClients)
                    {
                        continue;
                    }
                    RelayClient newClient;
                    newClient.m_address = from;
                    newClient.m_talkerId = m_nextTalkerId++;
                    m_relayClients.push_back(newClient);
                    client = m_relayClients.end() - 1;
                    AZLOG_INFO("VoiceChat: relay client %u joined from %s", static_cast<AZ::u32>(newClient.m_talkerId), from.GetString().c_str());
                }
                client->m_channel = buffer[5];
                client->m_lastSeen = m_serverTime;
            }
            else if (type == TypeLeave && client != m_relayClients.end())
            {
                m_relayClients.erase(client);
            }
            else if (type == TypeVoiceToServer && received > static_cast<int32_t>(VoiceToServerHeader) &&
                     received <= static_cast<int32_t>(MaxVoiceToServer) && client != m_relayClients.end())
            {
                client->m_lastSeen = m_serverTime;
                // rewrite: [magic][TypeVoiceToClient][talkerId u16][seq u16][payload]
                AZ::u8 outPacket[MaxDatagram];
                WriteU32(outPacket, Magic);
                outPacket[4] = TypeVoiceToClient;
                WriteU16(outPacket + 5, client->m_talkerId);
                const size_t remainder = received - 5; // seq + payload
                memcpy(outPacket + 7, buffer + 5, remainder);
                const AZ::u32 outSize = aznumeric_cast<AZ::u32>(7 + remainder);
                AZ_Assert(outSize <= MaxDatagram, "relayed voice packet exceeds the datagram buffer");
                for (RelayClient& other : m_relayClients)
                {
                    if (other.m_talkerId != client->m_talkerId && other.m_channel == client->m_channel)
                    {
                        SendPacket(*m_serverSocket, other.m_address, outPacket, outSize);
                    }
                }
            }
        }

        AZStd::erase_if(
            m_relayClients,
            [this](const RelayClient& client) { return m_serverTime - client.m_lastSeen > ClientTimeout; });
    }

    void VoiceChatSystemComponent::TickClient(float deltaTime)
    {
        m_keepaliveTimer -= deltaTime;
        if (m_keepaliveTimer <= 0.0f)
        {
            m_keepaliveTimer = KeepaliveInterval;
            SendJoin();
        }

        // receive & play incoming voice
        AZ::u8 buffer[MaxDatagram];
        for (;;)
        {
            AzNetworking::IpAddress from;
            const int32_t received = m_clientSocket->Receive(from, buffer, sizeof(buffer));
            if (received <= 0)
            {
                break;
            }
            if (from != m_serverAddress)
            {
                continue; // only the relay we joined may deliver voice to us
            }
            if (received > static_cast<int32_t>(VoiceToClientHeader) && received <= static_cast<int32_t>(MaxVoiceToClient) &&
                ReadU32(buffer) == Magic && buffer[4] == TypeVoiceToClient && !m_muted)
            {
                const AZ::u32 talkerId = ReadU16(buffer + 5);
                PlayIncoming(talkerId, buffer + VoiceToClientHeader, received - VoiceToClientHeader);
            }
        }

        // talker "active" decay for HUD notifications
        for (auto& [talkerId, talker] : m_talkers)
        {
            talker.m_silence += deltaTime;
            if (talker.m_active && talker.m_silence > TalkerSilenceTimeout)
            {
                talker.m_active = false;
                VoiceChatNotificationBus::Broadcast(
                    &VoiceChatNotificationBus::Events::OnTalkerActive, talkerId, false);
            }
        }

        if (m_talking && m_micSessionActive)
        {
            CaptureAndSend(deltaTime);
        }
    }

    void VoiceChatSystemComponent::CaptureAndSend([[maybe_unused]] float deltaTime)
    {
        Audio::SAudioInputConfig targetConfig;
        targetConfig.m_sourceType = Audio::AudioInputSourceType::Microphone;
        targetConfig.m_sampleRate = SampleRate;
        targetConfig.m_numChannels = 1;
        targetConfig.m_bitsPerSample = 16;
        targetConfig.m_sampleType = Audio::AudioInputSampleType::Int;

        m_pcmScratch.resize(FrameSamples);
        for (int frame = 0; frame < 8; ++frame) // drain up to 160 ms per tick
        {
            void* dest = m_pcmScratch.data();
            AZStd::size_t frames = 0;
            Audio::MicrophoneRequestBus::BroadcastResult(
                frames, &Audio::MicrophoneRequestBus::Events::GetData, &dest, FrameSamples, targetConfig, false);
            if (frames == 0)
            {
                break;
            }

            if (static_cast<float>(voice_vad_threshold) > 0.0f)
            {
                double sumSquares = 0.0;
                for (size_t i = 0; i < frames; ++i)
                {
                    const double normalized = m_pcmScratch[i] / 32768.0;
                    sumSquares += normalized * normalized;
                }
                if (sqrt(sumSquares / frames) < static_cast<float>(voice_vad_threshold))
                {
                    continue; // below the activation threshold: drop the frame
                }
            }

            AZ::u8 packet[MaxDatagram];
            WriteU32(packet, Magic);
            packet[4] = TypeVoiceToServer;
            WriteU16(packet + 5, m_sendSequence++);
            for (size_t i = 0; i < frames; ++i)
            {
                packet[7 + i] = MuLaw::Encode(m_pcmScratch[i]);
            }
            SendPacket(*m_clientSocket, m_serverAddress, packet, aznumeric_cast<AZ::u32>(7 + frames));
        }
    }

    void VoiceChatSystemComponent::PlayIncoming(AZ::u32 talkerId, const AZ::u8* payload, size_t size)
    {
        auto* miniAudio = MiniAudio::MiniAudioInterface::Get();
        ma_engine* engine = miniAudio ? miniAudio->GetSoundEngine() : nullptr;
        if (!engine)
        {
            return;
        }

        if (m_talkers.find(talkerId) == m_talkers.end() && m_talkers.size() >= MaxTalkers)
        {
            return; // a hostile relay cannot make us allocate playback state without bound
        }

        Talker& talker = m_talkers[talkerId];
        if (!talker.m_ringBuffer)
        {
            talker.m_ringBuffer = new ma_pcm_rb();
            if (ma_pcm_rb_init(ma_format_s16, 1, RingBufferFrames, nullptr, nullptr, talker.m_ringBuffer) != MA_SUCCESS)
            {
                delete talker.m_ringBuffer;
                talker.m_ringBuffer = nullptr;
                return;
            }
            ma_pcm_rb_set_sample_rate(talker.m_ringBuffer, SampleRate);
            talker.m_sound = new ma_sound();
            if (ma_sound_init_from_data_source(engine, talker.m_ringBuffer, 0, nullptr, talker.m_sound) != MA_SUCCESS)
            {
                ma_pcm_rb_uninit(talker.m_ringBuffer);
                delete talker.m_ringBuffer;
                delete talker.m_sound;
                talker.m_ringBuffer = nullptr;
                talker.m_sound = nullptr;
                return;
            }
            ma_sound_set_looping(talker.m_sound, MA_TRUE); // keep alive across ring-buffer underruns
            ma_sound_set_volume(talker.m_sound, m_volume);
            ma_sound_start(talker.m_sound);
        }

        // decode into the ring buffer (may need two writes at the wrap point)
        size_t written = 0;
        while (written < size)
        {
            ma_uint32 frames = aznumeric_cast<ma_uint32>(size - written);
            void* writePointer = nullptr;
            if (ma_pcm_rb_acquire_write(talker.m_ringBuffer, &frames, &writePointer) != MA_SUCCESS || frames == 0)
            {
                break; // buffer full: drop the rest (better than growing latency)
            }
            AZ::s16* samples = static_cast<AZ::s16*>(writePointer);
            for (ma_uint32 i = 0; i < frames; ++i)
            {
                samples[i] = MuLaw::Decode(payload[written + i]);
            }
            ma_pcm_rb_commit_write(talker.m_ringBuffer, frames);
            written += frames;
        }

        talker.m_silence = 0.0f;
        if (!talker.m_active)
        {
            talker.m_active = true;
            VoiceChatNotificationBus::Broadcast(&VoiceChatNotificationBus::Events::OnTalkerActive, talkerId, true);
        }
    }

    void VoiceChatSystemComponent::DestroyTalker(Talker& talker)
    {
        if (talker.m_sound)
        {
            ma_sound_stop(talker.m_sound);
            ma_sound_uninit(talker.m_sound);
            delete talker.m_sound;
            talker.m_sound = nullptr;
        }
        if (talker.m_ringBuffer)
        {
            ma_pcm_rb_uninit(talker.m_ringBuffer);
            delete talker.m_ringBuffer;
            talker.m_ringBuffer = nullptr;
        }
    }
} // namespace VoiceChat
