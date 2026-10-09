/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Module/Module.h>

#include "VoxelNavSystemComponent.h"
#include "VoxelNavVolumeComponent.h"

namespace VoxelNav
{
    class VoxelNavModule
        : public AZ::Module
    {
    public:
        AZ_RTTI(VoxelNavModule, "{B7C8D9E0-F1A2-4B3C-8D4E-5F6A7B8C9D0E}", AZ::Module);
        AZ_CLASS_ALLOCATOR(VoxelNavModule, AZ::SystemAllocator);

        VoxelNavModule()
        {
            m_descriptors.insert(
                m_descriptors.end(),
                {
                    VoxelNavSystemComponent::CreateDescriptor(),
                    VoxelNavVolumeComponent::CreateDescriptor(),
                });
        }

        AZ::ComponentTypeList GetRequiredSystemComponents() const override
        {
            return AZ::ComponentTypeList{ azrtti_typeid<VoxelNavSystemComponent>() };
        }
    };
} // namespace VoxelNav

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME), VoxelNav::VoxelNavModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_VoxelNav, VoxelNav::VoxelNavModule)
#endif
