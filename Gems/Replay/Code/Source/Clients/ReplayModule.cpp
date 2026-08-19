/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Module/Module.h>

#include "ReplaySystemComponent.h"
#include "ReplayTrackerComponent.h"

namespace Replay
{
    class ReplayModule
        : public AZ::Module
    {
    public:
        AZ_RTTI(ReplayModule, "{AF843F19-191F-4305-9CDF-3028A4C04711}", AZ::Module);
        AZ_CLASS_ALLOCATOR(ReplayModule, AZ::SystemAllocator);

        ReplayModule()
        {
            m_descriptors.insert(
                m_descriptors.end(),
                {
                    ReplaySystemComponent::CreateDescriptor(),
                    ReplayTrackerComponent::CreateDescriptor(),
                });
        }

        AZ::ComponentTypeList GetRequiredSystemComponents() const override
        {
            return AZ::ComponentTypeList{ azrtti_typeid<ReplaySystemComponent>() };
        }
    };
} // namespace Replay

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME), Replay::ReplayModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_Replay, Replay::ReplayModule)
#endif
