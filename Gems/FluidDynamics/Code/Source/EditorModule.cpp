/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Module/Module.h>

#include "EditorFluidVolumeComponent.h"
#include "EditorWindComponent.h"
#include "FluidVolumeComponent.h"
#include "WindComponent.h"

namespace FluidDynamics
{
    class FluidDynamicsEditorModule
        : public AZ::Module
    {
    public:
        AZ_RTTI(FluidDynamicsEditorModule, "{9B3E5D70-1C2A-4E8F-A0B6-D7C8E9F0A1B2}", AZ::Module);
        AZ_CLASS_ALLOCATOR(FluidDynamicsEditorModule, AZ::SystemAllocator);

        FluidDynamicsEditorModule()
        {
            m_descriptors.insert(
                m_descriptors.end(),
                {
                    FluidVolumeComponent::CreateDescriptor(),
                    WindComponent::CreateDescriptor(),
                    EditorFluidVolumeComponent::CreateDescriptor(),
                    EditorWindComponent::CreateDescriptor(),
                });
        }
    };
} // namespace FluidDynamics

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME, _Editor), FluidDynamics::FluidDynamicsEditorModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_FluidDynamics_Editor, FluidDynamics::FluidDynamicsEditorModule)
#endif
