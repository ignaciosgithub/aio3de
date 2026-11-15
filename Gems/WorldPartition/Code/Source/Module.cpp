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

namespace WorldPartition
{
    class WorldPartitionModule : public AZ::Module
    {
    public:
        AZ_RTTI(WorldPartitionModule, "{6928D009-8379-4731-BB9D-1F5E19E51F16}", AZ::Module);
        AZ_CLASS_ALLOCATOR(WorldPartitionModule, AZ::SystemAllocator);
        WorldPartitionModule()
        {
            m_descriptors.insert(m_descriptors.end(), {
                WorldPartitionComponent::CreateDescriptor(), WorldPartitionPolicyComponent::CreateDescriptor(),
                WorldPartitionStreamingSourceComponent::CreateDescriptor(), WorldPartitionSystemComponent::CreateDescriptor() });
        }
        AZ::ComponentTypeList GetRequiredSystemComponents() const override
        {
            return { azrtti_typeid<WorldPartitionSystemComponent>() };
        }
    };
}

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME), WorldPartition::WorldPartitionModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_WorldPartition, WorldPartition::WorldPartitionModule)
#endif
