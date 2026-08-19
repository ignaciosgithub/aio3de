/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Math/Quaternion.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/RTTI/ReflectContext.h>
#include <AzCore/RTTI/TypeInfoSimple.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>

namespace Replay
{
    //! One sampled transform at a point in demo time.
    struct ReplayKeyframe
    {
        AZ_TYPE_INFO(ReplayKeyframe, "{FE6A2D9E-70EF-4F42-8C9E-E9BE0AFD0002}");

        static void Reflect(AZ::ReflectContext* context);

        float m_time = 0.0f;
        AZ::Vector3 m_position = AZ::Vector3::CreateZero();
        AZ::Quaternion m_rotation = AZ::Quaternion::CreateIdentity();
        float m_scale = 1.0f;
    };

    //! All keyframes recorded for one tracked entity.
    struct ReplayTrack
    {
        AZ_TYPE_INFO(ReplayTrack, "{67F01415-FDDE-4587-AA84-42D324E1C2AE}");

        static void Reflect(AZ::ReflectContext* context);

        AZStd::string m_trackName;
        AZStd::vector<ReplayKeyframe> m_keyframes;
    };

    //! A complete demo: every track recorded during one session.
    struct ReplayData
    {
        AZ_TYPE_INFO(ReplayData, "{8CC3E208-9314-444D-8A8A-4CB056F8ACE5}");

        static void Reflect(AZ::ReflectContext* context);

        float m_duration = 0.0f;
        AZStd::vector<ReplayTrack> m_tracks;
    };
} // namespace Replay
