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

#include <Replay/ReplayBus.h>

#include "ReplayData.h"

namespace Replay
{
    //! Records tracked entities into demo files and plays demos back, driving the
    //! recorded entities' transforms (Quake demo style).
    class ReplaySystemComponent final
        : public AZ::Component
        , private AZ::TickBus::Handler
        , private ReplayRequestBus::Handler
        , private ReplayTrackerRegistrationBus::Handler
    {
    public:
        AZ_COMPONENT(ReplaySystemComponent, "{87EACB17-7537-4715-870F-29EC2401BBB4}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);

        // AZ::Component
        void Activate() override;
        void Deactivate() override;

        // ReplayRequestBus
        bool StartRecording(const AZStd::string& demoName) override;
        AZStd::string StopRecording() override;
        bool StartPlayback(const AZStd::string& demoName) override;
        void StopPlayback() override;
        void SetPlaybackPaused(bool paused) override;
        void SeekPlayback(float timeSeconds) override;
        void SetPlaybackSpeed(float speed) override;
        bool IsRecording() const override;
        bool IsPlayingBack() const override;
        float GetPlaybackTime() const override;
        float GetPlaybackDuration() const override;

        // ReplayTrackerRegistrationBus
        void RegisterTracker(AZ::EntityId entityId, const AZStd::string& trackName, float sampleRateFps) override;
        void UnregisterTracker(AZ::EntityId entityId) override;

    private:
        // AZ::TickBus
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;

        void UpdateRecording(float deltaTime);
        void UpdatePlayback(float deltaTime);
        void ApplyPlaybackAtCurrentTime();
        static AZStd::string GetDemoFilePath(const AZStd::string& demoName);

        struct TrackerState
        {
            AZStd::string m_trackName;
            float m_sampleRateFps = 30.0f;
            float m_accumulator = 0.0f;
            size_t m_trackIndex = 0;
            bool m_hasTrack = false;
        };

        AZStd::unordered_map<AZ::EntityId, TrackerState> m_trackers;

        bool m_recording = false;
        AZStd::string m_recordingName;
        float m_recordTime = 0.0f;
        ReplayData m_recordData;

        bool m_playing = false;
        bool m_paused = false;
        float m_playbackTime = 0.0f;
        float m_playbackSpeed = 1.0f;
        ReplayData m_playbackData;
        AZStd::unordered_map<size_t, AZ::EntityId> m_playbackBindings; //!< track index -> entity
    };
} // namespace Replay
