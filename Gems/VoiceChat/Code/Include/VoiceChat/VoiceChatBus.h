/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzCore/EBus/EBus.h>
#include <AzCore/std/string/string.h>

namespace VoiceChat
{
    //! Client-side voice chat control.
    class VoiceChatRequests
        : public AZ::EBusTraits
    {
    public:
        static const AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Single;
        static const AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::Single;

        //! Connects to the voice relay on the game server and joins a channel
        //! (use the team id as the channel for team-only voice).
        virtual void ConnectVoice(const AZStd::string& address, AZ::u32 port, AZ::u32 channel) = 0;
        //! Leaves the relay and stops capture/playback.
        virtual void DisconnectVoice() = 0;
        //! Switches channel (team) without reconnecting.
        virtual void SetChannel(AZ::u32 channel) = 0;
        //! Push-to-talk: capture and send while true.
        virtual void SetTalking(bool talking) = 0;
        virtual bool IsTalking() = 0;
        //! Mutes all incoming voice (playback).
        virtual void SetMuted(bool muted) = 0;
        virtual bool IsMuted() = 0;
        //! Playback volume for incoming voice (1 = default).
        virtual void SetVoiceVolume(float volume) = 0;
    };
    using VoiceChatRequestBus = AZ::EBus<VoiceChatRequests>;

    class VoiceChatNotifications
        : public AZ::EBusTraits
    {
    public:
        static const AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Multiple;
        static const AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::Single;

        //! A remote talker's voice started/stopped arriving (for HUD speaker icons).
        virtual void OnTalkerActive([[maybe_unused]] AZ::u32 talkerId, [[maybe_unused]] bool active) {}
    };
    using VoiceChatNotificationBus = AZ::EBus<VoiceChatNotifications>;
} // namespace VoiceChat
