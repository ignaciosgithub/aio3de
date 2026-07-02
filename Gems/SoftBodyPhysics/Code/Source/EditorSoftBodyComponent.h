/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzToolsFramework/ToolsComponents/EditorComponentBase.h>

#include "SoftBodySettings.h"

namespace SoftBodyPhysics
{
    //! Editor counterpart of SoftBodyComponent: exposes the tunables in the entity inspector
    //! and exports the runtime component into the game entity. Simulation only runs in game mode.
    class EditorSoftBodyComponent
        : public AzToolsFramework::Components::EditorComponentBase
    {
    public:
        AZ_EDITOR_COMPONENT(EditorSoftBodyComponent, "{0D9E8F7A-6B5C-4D3E-A2B1-C0D9E8F7A6B5}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required);

        // EditorComponentBase
        void BuildGameEntity(AZ::Entity* gameEntity) override;

    private:
        SoftBodySettings m_settings;
    };
} // namespace SoftBodyPhysics
