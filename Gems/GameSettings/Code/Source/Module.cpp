/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include <AzCore/Memory/SystemAllocator.h>
#include <AzCore/Module/Module.h>

#include "GameSettingsSystemComponent.h"
#include "RemappableInputComponent.h"

namespace GameSettings
{
    class GameSettingsModule
        : public AZ::Module
    {
    public:
        AZ_RTTI(GameSettingsModule, "{A4C6E1F8-2B93-47D5-8E60-19FD3B57A2C4}", AZ::Module);
        AZ_CLASS_ALLOCATOR(GameSettingsModule, AZ::SystemAllocator);

        GameSettingsModule()
        {
            m_descriptors.insert(m_descriptors.end(), {
                GameSettingsSystemComponent::CreateDescriptor(),
                RemappableInputComponent::CreateDescriptor(),
            });
        }

        AZ::ComponentTypeList GetRequiredSystemComponents() const override
        {
            return AZ::ComponentTypeList{ azrtti_typeid<GameSettingsSystemComponent>() };
        }
    };
} // namespace GameSettings

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME), GameSettings::GameSettingsModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_GameSettings, GameSettings::GameSettingsModule)
#endif
