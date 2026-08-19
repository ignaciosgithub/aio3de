/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/std/string/string.h>

namespace Replay
{
    //! Serialized settings for the Replay Tracker component.
    class ReplayTrackerConfig final
    {
    public:
        AZ_TYPE_INFO(ReplayTrackerConfig, "{EC028E16-0DEB-447B-96D3-071A6D58D95B}");

        static void Reflect(AZ::ReflectContext* context);

        //! Optional track name; when empty the entity name is used. Playback matches
        //! entities by this name, so keep it unique for reliable playback.
        AZStd::string m_trackName;

        //! How many transform samples per second to record. 0 records every frame.
        float m_sampleRateFps = 30.0f;
    };

    //! Opt-in marker: entities with this component get their transform recorded into
    //! demo files by the replay system while a recording is active.
    class ReplayTrackerComponent final
        : public AZ::Component
    {
    public:
        AZ_COMPONENT(ReplayTrackerComponent, "{EA81976A-FC7B-447D-813D-822F06FDF95E}");

        ReplayTrackerComponent() = default;
        explicit ReplayTrackerComponent(const ReplayTrackerConfig& config);

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);

        // AZ::Component
        void Activate() override;
        void Deactivate() override;

    private:
        ReplayTrackerConfig m_config;
    };
} // namespace Replay
