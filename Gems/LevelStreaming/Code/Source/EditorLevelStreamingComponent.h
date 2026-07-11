/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzToolsFramework/ToolsComponents/EditorComponentBase.h>

#include "LevelStreamingSettings.h"

namespace LevelStreaming
{
    //! Editor counterpart of LevelStreamingComponent: exposes the tunables in the entity inspector
    //! and exports the runtime component into the game entity. Streaming only runs in game mode.
    class EditorLevelStreamingComponent
        : public AzToolsFramework::Components::EditorComponentBase
    {
    public:
        AZ_EDITOR_COMPONENT(EditorLevelStreamingComponent, "{6D2C4B8A-0E1F-4A3D-9B5C-7F8E9D0A1B2C}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);

        // EditorComponentBase
        void BuildGameEntity(AZ::Entity* gameEntity) override;

    private:
        LevelStreamingSettings m_settings;
    };
} // namespace LevelStreaming
