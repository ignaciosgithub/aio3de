/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include <AzCore/Module/Module.h>
#include <AzCore/RTTI/RTTI.h>

#include <ServerAdminSystemComponent.h>

namespace ServerAdmin
{
    class ServerAdminModule
        : public AZ::Module
    {
    public:
        AZ_RTTI(ServerAdminModule, "{7E93C215-4A6B-4D80-B1F7-52E9A0D6C348}", AZ::Module);
        AZ_CLASS_ALLOCATOR(ServerAdminModule, AZ::SystemAllocator);

        ServerAdminModule()
        {
            m_descriptors.insert(m_descriptors.end(), {
                ServerAdminSystemComponent::CreateDescriptor(),
            });
        }

        AZ::ComponentTypeList GetRequiredSystemComponents() const override
        {
            return AZ::ComponentTypeList{
                azrtti_typeid<ServerAdminSystemComponent>(),
            };
        }
    };
} // namespace ServerAdmin

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME), ServerAdmin::ServerAdminModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_ServerAdmin, ServerAdmin::ServerAdminModule)
#endif
