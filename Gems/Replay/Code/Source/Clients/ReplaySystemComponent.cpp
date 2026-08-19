/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "ReplaySystemComponent.h"

#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Console/IConsole.h>
#include <AzCore/IO/FileIO.h>
#include <AzCore/RTTI/BehaviorContext.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Serialization/Utils.h>
#include <AzCore/StringFunc/StringFunc.h>

#include "ReplayTrackerComponent.h"

namespace Replay
{
    void ReplaySystemComponent::Reflect(AZ::ReflectContext* context)
    {
        ReplayData::Reflect(context);

        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<ReplaySystemComponent, AZ::Component>()->Version(1);
        }

        if (auto* behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behaviorContext->EBus<ReplayRequestBus>("ReplayRequestBus")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Category, "Replay")
                ->Attribute(AZ::Script::Attributes::Module, "replay")
                ->Event("StartRecording", &ReplayRequestBus::Events::StartRecording)
                ->Event("StopRecording", &ReplayRequestBus::Events::StopRecording)
                ->Event("StartPlayback", &ReplayRequestBus::Events::StartPlayback)
                ->Event("StopPlayback", &ReplayRequestBus::Events::StopPlayback)
                ->Event("SetPlaybackPaused", &ReplayRequestBus::Events::SetPlaybackPaused)
                ->Event("SeekPlayback", &ReplayRequestBus::Events::SeekPlayback)
                ->Event("SetPlaybackSpeed", &ReplayRequestBus::Events::SetPlaybackSpeed)
                ->Event("IsRecording", &ReplayRequestBus::Events::IsRecording)
                ->Event("IsPlayingBack", &ReplayRequestBus::Events::IsPlayingBack)
                ->Event("GetPlaybackTime", &ReplayRequestBus::Events::GetPlaybackTime)
                ->Event("GetPlaybackDuration", &ReplayRequestBus::Events::GetPlaybackDuration);
        }
    }

    void ReplaySystemComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("ReplaySystemService"));
    }

    void ReplaySystemComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("ReplaySystemService"));
    }

    void ReplaySystemComponent::Activate()
    {
        ReplayRequestBus::Handler::BusConnect();
        ReplayTrackerRegistrationBus::Handler::BusConnect();
        AZ::TickBus::Handler::BusConnect();
    }

    void ReplaySystemComponent::Deactivate()
    {
        AZ::TickBus::Handler::BusDisconnect();
        ReplayTrackerRegistrationBus::Handler::BusDisconnect();
        ReplayRequestBus::Handler::BusDisconnect();

        m_recording = false;
        m_playing = false;
        m_trackers.clear();
    }

    void ReplaySystemComponent::RegisterTracker(AZ::EntityId entityId, const AZStd::string& trackName, float sampleRateFps)
    {
        TrackerState state;
        state.m_trackName = trackName;
        state.m_sampleRateFps = sampleRateFps;

        if (m_recording)
        {
            ReplayTrack track;
            track.m_trackName = trackName;
            m_recordData.m_tracks.push_back(AZStd::move(track));
            state.m_trackIndex = m_recordData.m_tracks.size() - 1;
            state.m_hasTrack = true;
        }

        m_trackers[entityId] = AZStd::move(state);
    }

    void ReplaySystemComponent::UnregisterTracker(AZ::EntityId entityId)
    {
        m_trackers.erase(entityId);
    }

    AZStd::string ReplaySystemComponent::GetDemoFilePath(const AZStd::string& demoName)
    {
        AZStd::string aliasedPath = AZStd::string::format("@user@/Replays/%s.replay", demoName.c_str());

        auto* fileIo = AZ::IO::FileIOBase::GetInstance();
        if (!fileIo)
        {
            return aliasedPath;
        }

        AZ::IO::FixedMaxPath resolvedPath;
        if (!fileIo->ResolvePath(resolvedPath, aliasedPath.c_str()))
        {
            return aliasedPath;
        }

        return AZStd::string(resolvedPath.Native());
    }

    bool ReplaySystemComponent::StartRecording(const AZStd::string& demoName)
    {
        if (m_recording)
        {
            AZ_Warning("Replay", false, "Already recording '%s'; stop it before starting a new recording", m_recordingName.c_str());
            return false;
        }

        if (demoName.empty())
        {
            AZ_Warning("Replay", false, "A demo name is required to start recording");
            return false;
        }

        if (m_trackers.empty())
        {
            AZ_Warning("Replay", false, "No active entities with a Replay Tracker component; nothing to record");
            return false;
        }

        m_playing = false;
        m_recording = true;
        m_recordingName = demoName;
        m_recordTime = 0.0f;
        m_recordData = ReplayData();

        for (auto& [entityId, tracker] : m_trackers)
        {
            ReplayTrack track;
            track.m_trackName = tracker.m_trackName;
            m_recordData.m_tracks.push_back(AZStd::move(track));
            tracker.m_trackIndex = m_recordData.m_tracks.size() - 1;
            tracker.m_hasTrack = true;
            tracker.m_accumulator = 0.0f;
        }

        UpdateRecording(0.0f); // capture the initial pose at t=0

        AZ_Printf("Replay", "Recording demo '%s' (%zu tracked entities)", demoName.c_str(), m_trackers.size());
        return true;
    }

    AZStd::string ReplaySystemComponent::StopRecording()
    {
        if (!m_recording)
        {
            return {};
        }

        // capture the final pose so the demo ends exactly where the entities stopped
        for (auto& [entityId, tracker] : m_trackers)
        {
            tracker.m_accumulator = AZStd::numeric_limits<float>::max();
        }
        UpdateRecording(0.0f);

        m_recording = false;
        m_recordData.m_duration = m_recordTime;

        const AZStd::string filePath = GetDemoFilePath(m_recordingName);

        AZStd::string folder = filePath;
        AZ::StringFunc::Path::StripFullName(folder);
        if (auto* fileIo = AZ::IO::FileIOBase::GetInstance())
        {
            fileIo->CreatePath(folder.c_str());
        }

        if (!AZ::Utils::SaveObjectToFile(filePath, AZ::DataStream::ST_XML, &m_recordData))
        {
            AZ_Warning("Replay", false, "Failed to save demo to '%s'", filePath.c_str());
            return {};
        }

        AZ_Printf("Replay", "Saved demo '%s' (%.2fs) to %s", m_recordingName.c_str(), m_recordData.m_duration, filePath.c_str());
        return filePath;
    }

    bool ReplaySystemComponent::StartPlayback(const AZStd::string& demoName)
    {
        if (m_recording)
        {
            AZ_Warning("Replay", false, "Cannot play back while recording");
            return false;
        }

        const AZStd::string filePath = GetDemoFilePath(demoName);

        ReplayData loaded;
        if (!AZ::Utils::LoadObjectFromFileInPlace(filePath, loaded))
        {
            AZ_Warning("Replay", false, "Failed to load demo from '%s'", filePath.c_str());
            return false;
        }

        m_playbackData = AZStd::move(loaded);
        m_playbackBindings.clear();

        // bind tracks to entities: registered trackers first, then any entity with a matching name
        for (size_t trackIndex = 0; trackIndex < m_playbackData.m_tracks.size(); ++trackIndex)
        {
            const AZStd::string& trackName = m_playbackData.m_tracks[trackIndex].m_trackName;

            for (const auto& [entityId, tracker] : m_trackers)
            {
                if (tracker.m_trackName == trackName)
                {
                    m_playbackBindings[trackIndex] = entityId;
                    break;
                }
            }

            if (!m_playbackBindings.contains(trackIndex))
            {
                AZ::ComponentApplicationBus::Broadcast(
                    &AZ::ComponentApplicationBus::Events::EnumerateEntities,
                    [this, trackIndex, &trackName](AZ::Entity* entity)
                    {
                        if (!m_playbackBindings.contains(trackIndex) &&
                            entity->GetState() == AZ::Entity::State::Active &&
                            entity->GetName() == trackName)
                        {
                            m_playbackBindings[trackIndex] = entity->GetId();
                        }
                    });
            }

            AZ_Warning("Replay", m_playbackBindings.contains(trackIndex),
                "No entity found for track '%s'; that track will not play", trackName.c_str());
        }

        if (m_playbackBindings.empty())
        {
            AZ_Warning("Replay", false, "No demo tracks could be bound to entities; playback aborted");
            return false;
        }

        m_playing = true;
        m_paused = false;
        m_playbackTime = 0.0f;
        ApplyPlaybackAtCurrentTime();

        AZ_Printf("Replay", "Playing demo '%s' (%.2fs, %zu/%zu tracks bound)",
            demoName.c_str(), m_playbackData.m_duration, m_playbackBindings.size(), m_playbackData.m_tracks.size());
        return true;
    }

    void ReplaySystemComponent::StopPlayback()
    {
        m_playing = false;
        m_playbackBindings.clear();
    }

    void ReplaySystemComponent::SetPlaybackPaused(bool paused)
    {
        m_paused = paused;
    }

    void ReplaySystemComponent::SeekPlayback(float timeSeconds)
    {
        if (m_playing)
        {
            m_playbackTime = AZStd::clamp(timeSeconds, 0.0f, m_playbackData.m_duration);
            ApplyPlaybackAtCurrentTime();
        }
    }

    void ReplaySystemComponent::SetPlaybackSpeed(float speed)
    {
        m_playbackSpeed = AZStd::max(speed, 0.0f);
    }

    bool ReplaySystemComponent::IsRecording() const
    {
        return m_recording;
    }

    bool ReplaySystemComponent::IsPlayingBack() const
    {
        return m_playing;
    }

    float ReplaySystemComponent::GetPlaybackTime() const
    {
        return m_playbackTime;
    }

    float ReplaySystemComponent::GetPlaybackDuration() const
    {
        return m_playbackData.m_duration;
    }

    void ReplaySystemComponent::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        if (m_recording)
        {
            m_recordTime += deltaTime;
            UpdateRecording(deltaTime);
        }
        else if (m_playing && !m_paused)
        {
            UpdatePlayback(deltaTime);
        }
    }

    void ReplaySystemComponent::UpdateRecording(float deltaTime)
    {
        for (auto& [entityId, tracker] : m_trackers)
        {
            if (!tracker.m_hasTrack)
            {
                continue;
            }

            tracker.m_accumulator += deltaTime;

            const float interval = tracker.m_sampleRateFps > 0.0f ? (1.0f / tracker.m_sampleRateFps) : 0.0f;
            if (tracker.m_accumulator < interval)
            {
                continue;
            }
            tracker.m_accumulator = interval > 0.0f ? AZStd::min(tracker.m_accumulator - interval, interval) : 0.0f;

            AZ::Transform worldTm = AZ::Transform::CreateIdentity();
            AZ::TransformBus::EventResult(worldTm, entityId, &AZ::TransformBus::Events::GetWorldTM);

            ReplayKeyframe keyframe;
            keyframe.m_time = m_recordTime;
            keyframe.m_position = worldTm.GetTranslation();
            keyframe.m_rotation = worldTm.GetRotation();
            keyframe.m_scale = worldTm.GetUniformScale();

            auto& keyframes = m_recordData.m_tracks[tracker.m_trackIndex].m_keyframes;
            if (keyframes.empty() || keyframes.back().m_time < m_recordTime)
            {
                keyframes.push_back(keyframe);
            }
        }
    }

    void ReplaySystemComponent::UpdatePlayback(float deltaTime)
    {
        m_playbackTime += deltaTime * m_playbackSpeed;
        if (m_playbackTime >= m_playbackData.m_duration)
        {
            m_playbackTime = m_playbackData.m_duration;
            ApplyPlaybackAtCurrentTime();
            m_playing = false;
            AZ_Printf("Replay", "Demo playback finished");
            return;
        }

        ApplyPlaybackAtCurrentTime();
    }

    void ReplaySystemComponent::ApplyPlaybackAtCurrentTime()
    {
        for (const auto& [trackIndex, entityId] : m_playbackBindings)
        {
            const auto& keyframes = m_playbackData.m_tracks[trackIndex].m_keyframes;
            if (keyframes.empty())
            {
                continue;
            }

            // find the first keyframe at or after the current time
            auto next = AZStd::lower_bound(keyframes.begin(), keyframes.end(), m_playbackTime,
                [](const ReplayKeyframe& keyframe, float t)
                {
                    return keyframe.m_time < t;
                });

            ReplayKeyframe sample;
            if (next == keyframes.begin())
            {
                sample = keyframes.front();
            }
            else if (next == keyframes.end())
            {
                sample = keyframes.back();
            }
            else
            {
                const ReplayKeyframe& a = *(next - 1);
                const ReplayKeyframe& b = *next;
                const float span = b.m_time - a.m_time;
                const float t = span > 0.0f ? (m_playbackTime - a.m_time) / span : 1.0f;

                sample.m_time = m_playbackTime;
                sample.m_position = a.m_position.Lerp(b.m_position, t);
                sample.m_rotation = a.m_rotation.Slerp(b.m_rotation, t);
                sample.m_scale = AZ::Lerp(a.m_scale, b.m_scale, t);
            }

            AZ::Transform worldTm = AZ::Transform::CreateFromQuaternionAndTranslation(sample.m_rotation, sample.m_position);
            worldTm.SetUniformScale(sample.m_scale);
            AZ::TransformBus::Event(entityId, &AZ::TransformBus::Events::SetWorldTM, worldTm);
        }
    }

    static void ReplayRecordCommand(const AZ::ConsoleCommandContainer& arguments)
    {
        if (arguments.empty())
        {
            AZ_Printf("Replay", "Usage: replay_record <demoName>");
            return;
        }
        bool started = false;
        ReplayRequestBus::BroadcastResult(started, &ReplayRequests::StartRecording, AZStd::string(arguments.front()));
    }
    AZ_CONSOLEFREEFUNC("replay_record", ReplayRecordCommand, AZ::ConsoleFunctorFlags::DontReplicate, "Start recording tracked entities into a demo: replay_record <demoName>");

    static void ReplayStopCommand([[maybe_unused]] const AZ::ConsoleCommandContainer& arguments)
    {
        AZStd::string savedPath;
        ReplayRequestBus::BroadcastResult(savedPath, &ReplayRequests::StopRecording);
    }
    AZ_CONSOLEFREEFUNC("replay_stop", ReplayStopCommand, AZ::ConsoleFunctorFlags::DontReplicate, "Stop recording and save the demo file");

    static void ReplayPlayCommand(const AZ::ConsoleCommandContainer& arguments)
    {
        if (arguments.empty())
        {
            AZ_Printf("Replay", "Usage: replay_play <demoName>");
            return;
        }
        bool started = false;
        ReplayRequestBus::BroadcastResult(started, &ReplayRequests::StartPlayback, AZStd::string(arguments.front()));
    }
    AZ_CONSOLEFREEFUNC("replay_play", ReplayPlayCommand, AZ::ConsoleFunctorFlags::DontReplicate, "Play back a recorded demo: replay_play <demoName>");

    static void ReplayStopPlaybackCommand([[maybe_unused]] const AZ::ConsoleCommandContainer& arguments)
    {
        ReplayRequestBus::Broadcast(&ReplayRequests::StopPlayback);
    }
    AZ_CONSOLEFREEFUNC("replay_stop_playback", ReplayStopPlaybackCommand, AZ::ConsoleFunctorFlags::DontReplicate, "Stop demo playback");

    static void ReplayPauseCommand(const AZ::ConsoleCommandContainer& arguments)
    {
        const bool pause = arguments.empty() || arguments.front() != "0";
        ReplayRequestBus::Broadcast(&ReplayRequests::SetPlaybackPaused, pause);
    }
    AZ_CONSOLEFREEFUNC("replay_pause", ReplayPauseCommand, AZ::ConsoleFunctorFlags::DontReplicate, "Pause (replay_pause) or resume (replay_pause 0) demo playback");

    static void ReplaySeekCommand(const AZ::ConsoleCommandContainer& arguments)
    {
        if (arguments.empty())
        {
            AZ_Printf("Replay", "Usage: replay_seek <seconds>");
            return;
        }
        const float seconds = static_cast<float>(atof(AZStd::string(arguments.front()).c_str()));
        ReplayRequestBus::Broadcast(&ReplayRequests::SeekPlayback, seconds);
    }
    AZ_CONSOLEFREEFUNC("replay_seek", ReplaySeekCommand, AZ::ConsoleFunctorFlags::DontReplicate, "Jump demo playback to a time in seconds: replay_seek <seconds>");

    static void ReplaySpeedCommand(const AZ::ConsoleCommandContainer& arguments)
    {
        if (arguments.empty())
        {
            AZ_Printf("Replay", "Usage: replay_speed <multiplier>");
            return;
        }
        const float speed = static_cast<float>(atof(AZStd::string(arguments.front()).c_str()));
        ReplayRequestBus::Broadcast(&ReplayRequests::SetPlaybackSpeed, speed);
    }
    AZ_CONSOLEFREEFUNC("replay_speed", ReplaySpeedCommand, AZ::ConsoleFunctorFlags::DontReplicate, "Set demo playback speed multiplier: replay_speed <multiplier>");
} // namespace Replay
