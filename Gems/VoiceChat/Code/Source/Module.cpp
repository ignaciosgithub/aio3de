/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include <AzCore/Memory/SystemAllocator.h>
#include <AzCore/Module/Module.h>

#include "VoiceChatSystemComponent.h"

namespace VoiceChat
{
    class VoiceChatModule
        : public AZ::Module
    {
    public:
        AZ_RTTI(VoiceChatModule, "{8C4B21E7-3FA9-4D6B-95E0-2B7D410C68A3}", AZ::Module);
        AZ_CLASS_ALLOCATOR(VoiceChatModule, AZ::SystemAllocator);

        VoiceChatModule()
        {
            m_descriptors.insert(m_descriptors.end(), {
                VoiceChatSystemComponent::CreateDescriptor(),
            });
        }

        AZ::ComponentTypeList GetRequiredSystemComponents() const override
        {
            return AZ::ComponentTypeList{ azrtti_typeid<VoiceChatSystemComponent>() };
        }
    };
} // namespace VoiceChat

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME), VoiceChat::VoiceChatModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_VoiceChat, VoiceChat::VoiceChatModule)
#endif
