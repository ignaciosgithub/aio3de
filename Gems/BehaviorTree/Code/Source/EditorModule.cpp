/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Module/Module.h>

#include "BehaviorTreeEditorSystemComponent.h"
#include "BehaviorTreeComponent.h"
#include "EditorBehaviorTreeComponent.h"

namespace BehaviorTree
{
    class BehaviorTreeEditorModule
        : public AZ::Module
    {
    public:
        AZ_RTTI(BehaviorTreeEditorModule, "{E1D1A8E9-0F43-47EC-9AB4-4C46C073F3C0}", AZ::Module);
        AZ_CLASS_ALLOCATOR(BehaviorTreeEditorModule, AZ::SystemAllocator);

        BehaviorTreeEditorModule()
        {
            m_descriptors.insert(
                m_descriptors.end(),
                {
                    BehaviorTreeComponent::CreateDescriptor(),
                    BehaviorTreeEditorSystemComponent::CreateDescriptor(),
                    EditorBehaviorTreeComponent::CreateDescriptor(),
                });
        }

        AZ::ComponentTypeList GetRequiredSystemComponents() const override
        {
            return AZ::ComponentTypeList{ azrtti_typeid<BehaviorTreeEditorSystemComponent>() };
        }
    };
} // namespace BehaviorTree

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME, _Editor), BehaviorTree::BehaviorTreeEditorModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_BehaviorTree_Editor, BehaviorTree::BehaviorTreeEditorModule)
#endif
