/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Module/Module.h>

#include "BehaviorTreeComponent.h"

namespace BehaviorTree
{
    class BehaviorTreeModule
        : public AZ::Module
    {
    public:
        AZ_RTTI(BehaviorTreeModule, "{DAA9A3A8-3D15-4B8E-8447-2C9F1CB03AA0}", AZ::Module);
        AZ_CLASS_ALLOCATOR(BehaviorTreeModule, AZ::SystemAllocator);

        BehaviorTreeModule()
        {
            m_descriptors.insert(m_descriptors.end(), { BehaviorTreeComponent::CreateDescriptor() });
        }
    };
} // namespace BehaviorTree

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME), BehaviorTree::BehaviorTreeModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_BehaviorTree, BehaviorTree::BehaviorTreeModule)
#endif
