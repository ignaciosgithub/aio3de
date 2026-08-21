/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzToolsFramework/ToolsComponents/EditorComponentBase.h>

#include "FluidSettings.h"

namespace FluidDynamics
{
    //! Editor counterpart of FluidVolumeComponent: exposes the tunables in the entity inspector
    //! and exports the runtime component into the game entity. Simulation only runs in game mode.
    class EditorFluidVolumeComponent
        : public AzToolsFramework::Components::EditorComponentBase
    {
    public:
        AZ_EDITOR_COMPONENT(EditorFluidVolumeComponent, "{B7E29C50-4D1A-4F8B-A6C3-0E9F2D5B8A17}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required);

        // EditorComponentBase
        void BuildGameEntity(AZ::Entity* gameEntity) override;

    private:
        FluidSettings m_settings;
    };
} // namespace FluidDynamics
