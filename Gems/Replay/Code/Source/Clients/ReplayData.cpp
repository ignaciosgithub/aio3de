/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "ReplayData.h"

#include <AzCore/Serialization/SerializeContext.h>

namespace Replay
{
    void ReplayKeyframe::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<ReplayKeyframe>()
                ->Version(1)
                ->Field("Time", &ReplayKeyframe::m_time)
                ->Field("Position", &ReplayKeyframe::m_position)
                ->Field("Rotation", &ReplayKeyframe::m_rotation)
                ->Field("Scale", &ReplayKeyframe::m_scale);
        }
    }

    void ReplayTrack::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<ReplayTrack>()
                ->Version(1)
                ->Field("TrackName", &ReplayTrack::m_trackName)
                ->Field("Keyframes", &ReplayTrack::m_keyframes);
        }
    }

    void ReplayData::Reflect(AZ::ReflectContext* context)
    {
        ReplayKeyframe::Reflect(context);
        ReplayTrack::Reflect(context);

        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<ReplayData>()
                ->Version(1)
                ->Field("Duration", &ReplayData::m_duration)
                ->Field("Tracks", &ReplayData::m_tracks);
        }
    }
} // namespace Replay
