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

namespace VideoTexture
{
    //! Controls the video texture attached to an entity.
    class VideoTextureRequests
        : public AZ::EBusTraits
    {
    public:
        static const AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::ById;
        using BusIdType = AZ::EntityId;

        virtual ~VideoTextureRequests() = default;

        //! Start or resume playback.
        virtual void Play() = 0;
        //! Pause playback, keeping the current frame on the texture.
        virtual void Pause() = 0;
        //! Stop playback and rewind to the beginning.
        virtual void Stop() = 0;
        //! Returns true while the video is playing.
        virtual bool IsPlaying() = 0;
        //! Whether the video restarts when it reaches the end.
        virtual void SetLooping(bool looping) = 0;
        virtual bool GetLooping() = 0;
        //! Playback speed multiplier (1.0 = normal speed).
        virtual void SetPlaybackSpeed(float speed) = 0;
        virtual float GetPlaybackSpeed() = 0;
    };

    using VideoTextureRequestBus = AZ::EBus<VideoTextureRequests>;
} // namespace VideoTexture
