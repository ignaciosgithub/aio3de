/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzToolsFramework/ToolsComponents/EditorComponentBase.h>

#include <Clients/VideoTextureComponent.h>

namespace VideoTexture
{
    //! Editor counterpart of VideoTextureComponent: exposes the settings in the entity inspector
    //! and exports the runtime component into the game entity. Playback only runs in game mode.
    class EditorVideoTextureComponent
        : public AzToolsFramework::Components::EditorComponentBase
    {
    public:
        AZ_EDITOR_COMPONENT(EditorVideoTextureComponent, "{755F2278-1858-47E4-9C5B-BDBB9CF51221}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);

        // EditorComponentBase
        void BuildGameEntity(AZ::Entity* gameEntity) override;

    private:
        VideoTextureConfig m_config;
    };
} // namespace VideoTexture
