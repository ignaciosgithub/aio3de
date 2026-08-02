/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include <AzCore/Module/Module.h>
#include <AzCore/RTTI/RTTI.h>

#include <Source/ArenaShooterNetSystemComponent.h>
#include <Source/AutoGen/AutoComponentTypes.h>

namespace ArenaShooterNet
{
    class ArenaShooterNetModule
        : public AZ::Module
    {
    public:
        AZ_RTTI(ArenaShooterNetModule, "{5D8A2E13-9B47-4C6F-A1E0-3F72B8D45C96}", AZ::Module);
        AZ_CLASS_ALLOCATOR(ArenaShooterNetModule, AZ::SystemAllocator);

        ArenaShooterNetModule()
        {
            m_descriptors.insert(m_descriptors.end(), {
                ArenaShooterNetSystemComponent::CreateDescriptor(),
            });
            CreateComponentDescriptors(m_descriptors); //< adds this gem's multiplayer components
        }

        AZ::ComponentTypeList GetRequiredSystemComponents() const override
        {
            return AZ::ComponentTypeList{
                azrtti_typeid<ArenaShooterNetSystemComponent>(),
            };
        }
    };
} // namespace ArenaShooterNet

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME), ArenaShooterNet::ArenaShooterNetModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_ArenaShooterNet, ArenaShooterNet::ArenaShooterNetModule)
#endif
