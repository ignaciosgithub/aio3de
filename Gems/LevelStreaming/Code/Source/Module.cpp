/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Module/Module.h>

#include "LevelStreamingComponent.h"

namespace LevelStreaming
{
    class LevelStreamingModule
        : public AZ::Module
    {
    public:
        AZ_RTTI(LevelStreamingModule, "{3F8A1B2C-4D5E-6F7A-8B9C-0D1E2F3A4B5C}", AZ::Module);
        AZ_CLASS_ALLOCATOR(LevelStreamingModule, AZ::SystemAllocator);

        LevelStreamingModule()
        {
            m_descriptors.insert(
                m_descriptors.end(),
                {
                    LevelStreamingComponent::CreateDescriptor(),
                });
        }
    };
} // namespace LevelStreaming

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME), LevelStreaming::LevelStreamingModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_LevelStreaming, LevelStreaming::LevelStreamingModule)
#endif
