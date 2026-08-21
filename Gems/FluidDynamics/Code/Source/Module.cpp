/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Module/Module.h>

#include "FluidVolumeComponent.h"
#include "WindComponent.h"

namespace FluidDynamics
{
    class FluidDynamicsModule
        : public AZ::Module
    {
    public:
        AZ_RTTI(FluidDynamicsModule, "{5E0D2C71-8A4B-4F3E-B6D9-0C1E2F3A4B5C}", AZ::Module);
        AZ_CLASS_ALLOCATOR(FluidDynamicsModule, AZ::SystemAllocator);

        FluidDynamicsModule()
        {
            m_descriptors.insert(
                m_descriptors.end(),
                {
                    FluidVolumeComponent::CreateDescriptor(),
                    WindComponent::CreateDescriptor(),
                });
        }
    };
} // namespace FluidDynamics

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME), FluidDynamics::FluidDynamicsModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_FluidDynamics, FluidDynamics::FluidDynamicsModule)
#endif
