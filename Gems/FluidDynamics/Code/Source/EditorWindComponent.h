/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzToolsFramework/ToolsComponents/EditorComponentBase.h>

#include "WindComponent.h"

namespace FluidDynamics
{
    //! Editor counterpart of WindComponent.
    class EditorWindComponent
        : public AzToolsFramework::Components::EditorComponentBase
    {
    public:
        AZ_EDITOR_COMPONENT(EditorWindComponent, "{D4A81F62-3C0E-45B9-8A7D-1B2C3E4F5A69}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);

        // EditorComponentBase
        void BuildGameEntity(AZ::Entity* gameEntity) override;

    private:
        WindSettings m_settings;
    };
} // namespace FluidDynamics
