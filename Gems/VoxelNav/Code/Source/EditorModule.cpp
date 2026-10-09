/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Module/Module.h>

#include "EditorVoxelNavVolumeComponent.h"
#include "VoxelNavSystemComponent.h"
#include "VoxelNavVolumeComponent.h"

namespace VoxelNav
{
    class VoxelNavEditorModule
        : public AZ::Module
    {
    public:
        AZ_RTTI(VoxelNavEditorModule, "{C8D9E0F1-A2B3-4C4D-9E5F-6A7B8C9D0E1F}", AZ::Module);
        AZ_CLASS_ALLOCATOR(VoxelNavEditorModule, AZ::SystemAllocator);

        VoxelNavEditorModule()
        {
            m_descriptors.insert(
                m_descriptors.end(),
                {
                    VoxelNavSystemComponent::CreateDescriptor(),
                    VoxelNavVolumeComponent::CreateDescriptor(),
                    EditorVoxelNavVolumeComponent::CreateDescriptor(),
                });
        }

        AZ::ComponentTypeList GetRequiredSystemComponents() const override
        {
            return AZ::ComponentTypeList{ azrtti_typeid<VoxelNavSystemComponent>() };
        }
    };
} // namespace VoxelNav

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME, _Editor), VoxelNav::VoxelNavEditorModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_VoxelNav_Editor, VoxelNav::VoxelNavEditorModule)
#endif
