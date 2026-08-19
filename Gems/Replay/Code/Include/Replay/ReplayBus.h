/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Component/EntityId.h>
#include <AzCore/EBus/EBus.h>
#include <AzCore/std/string/string.h>

namespace Replay
{
    //! Control interface for the replay system (Quake demo style recording and playback).
    //! Available from Lua, Script Canvas and the Editor Python console, plus the
    //! replay_* console commands.
    class ReplayRequests
        : public AZ::EBusTraits
    {
    public:
        static const AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Single;
        static const AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::Single;

        //! Start recording all entities with a Replay Tracker component into the named demo.
        //! The demo is saved to <project>/user/Replays/<name>.replay when recording stops.
        virtual bool StartRecording(const AZStd::string& demoName) = 0;

        //! Stop recording and save the demo file. Returns the saved file path (empty on failure).
        virtual AZStd::string StopRecording() = 0;

        //! Load the named demo (from <project>/user/Replays/) and start playing it back,
        //! driving the recorded entities' transforms. Entities are matched by track name.
        virtual bool StartPlayback(const AZStd::string& demoName) = 0;

        //! Stop playback, leaving entities where they currently are.
        virtual void StopPlayback() = 0;

        //! Pause or resume playback.
        virtual void SetPlaybackPaused(bool paused) = 0;

        //! Jump playback to a time (seconds, clamped to [0, duration]).
        virtual void SeekPlayback(float timeSeconds) = 0;

        //! Playback speed multiplier (1 = realtime, 0.5 = half speed, 2 = double).
        virtual void SetPlaybackSpeed(float speed) = 0;

        virtual bool IsRecording() const = 0;
        virtual bool IsPlayingBack() const = 0;
        virtual float GetPlaybackTime() const = 0;
        virtual float GetPlaybackDuration() const = 0;
    };

    using ReplayRequestBus = AZ::EBus<ReplayRequests>;

    //! Internal registration interface used by Replay Tracker components.
    class ReplayTrackerRegistrations
        : public AZ::EBusTraits
    {
    public:
        static const AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Single;
        static const AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::Single;

        virtual void RegisterTracker(AZ::EntityId entityId, const AZStd::string& trackName, float sampleRateFps) = 0;
        virtual void UnregisterTracker(AZ::EntityId entityId) = 0;
    };

    using ReplayTrackerRegistrationBus = AZ::EBus<ReplayTrackerRegistrations>;
} // namespace Replay
