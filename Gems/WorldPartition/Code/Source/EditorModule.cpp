/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Module/Module.h>
#include "WorldPartitionComponent.h"
#include "WorldPartitionPolicyComponent.h"
#include "WorldPartitionStreamingSourceComponent.h"
#include "WorldPartitionSystemComponent.h"
#include "EditorWorldPartitionComponents.h"
#include "WorldPartitionEditorSystemComponent.h"

namespace WorldPartition
{
    class WorldPartitionEditorModule : public AZ::Module
    {
    public:
        AZ_RTTI(WorldPartitionEditorModule, "{EA35A1CB-BA69-42C5-933D-6F6DEAA486E1}", AZ::Module);
        AZ_CLASS_ALLOCATOR(WorldPartitionEditorModule, AZ::SystemAllocator);
        WorldPartitionEditorModule()
        {
            m_descriptors.insert(m_descriptors.end(), {
                WorldPartitionComponent::CreateDescriptor(), WorldPartitionPolicyComponent::CreateDescriptor(),
                WorldPartitionStreamingSourceComponent::CreateDescriptor(), WorldPartitionSystemComponent::CreateDescriptor(),
                WorldPartitionEditorSystemComponent::CreateDescriptor(), EditorWorldPartitionComponent::CreateDescriptor(),
                EditorWorldPartitionPolicyComponent::CreateDescriptor(), EditorWorldPartitionStreamingSourceComponent::CreateDescriptor() });
        }
        AZ::ComponentTypeList GetRequiredSystemComponents() const override
        {
            return { azrtti_typeid<WorldPartitionEditorSystemComponent>() };
        }
    };
}

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME, _Editor), WorldPartition::WorldPartitionEditorModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_WorldPartition_Editor, WorldPartition::WorldPartitionEditorModule)
#endif
