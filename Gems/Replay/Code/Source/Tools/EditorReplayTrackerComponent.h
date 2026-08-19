/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzToolsFramework/ToolsComponents/EditorComponentBase.h>

#include <Clients/ReplayTrackerComponent.h>

namespace Replay
{
    //! Editor counterpart of ReplayTrackerComponent: exposes the settings in the entity
    //! inspector and exports the runtime component into the game entity.
    class EditorReplayTrackerComponent
        : public AzToolsFramework::Components::EditorComponentBase
    {
    public:
        AZ_EDITOR_COMPONENT(EditorReplayTrackerComponent, "{82B5BF0E-A579-4B97-90C4-380765E8B817}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);

        // EditorComponentBase
        void BuildGameEntity(AZ::Entity* gameEntity) override;

    private:
        ReplayTrackerConfig m_config;
    };
} // namespace Replay
