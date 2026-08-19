/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Module/Module.h>

#include <Clients/ReplaySystemComponent.h>
#include <Clients/ReplayTrackerComponent.h>
#include <Tools/EditorReplayTrackerComponent.h>

namespace Replay
{
    class ReplayEditorModule
        : public AZ::Module
    {
    public:
        AZ_RTTI(ReplayEditorModule, "{31677F00-6E0B-4AE2-AE3E-56436CA77685}", AZ::Module);
        AZ_CLASS_ALLOCATOR(ReplayEditorModule, AZ::SystemAllocator);

        ReplayEditorModule()
        {
            m_descriptors.insert(
                m_descriptors.end(),
                {
                    ReplaySystemComponent::CreateDescriptor(),
                    ReplayTrackerComponent::CreateDescriptor(),
                    EditorReplayTrackerComponent::CreateDescriptor(),
                });
        }

        AZ::ComponentTypeList GetRequiredSystemComponents() const override
        {
            return AZ::ComponentTypeList{ azrtti_typeid<ReplaySystemComponent>() };
        }
    };
} // namespace Replay

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME), Replay::ReplayEditorModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_Replay_Editor, Replay::ReplayEditorModule)
#endif
