/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include <AzCore/Memory/SystemAllocator.h>
#include <AzCore/Module/Module.h>

#include "ServerBrowserSystemComponent.h"

namespace ServerBrowser
{
    class ServerBrowserModule
        : public AZ::Module
    {
    public:
        AZ_RTTI(ServerBrowserModule, "{6E1F03B7-9D45-4A82-B3C0-58AF7D21E964}", AZ::Module);
        AZ_CLASS_ALLOCATOR(ServerBrowserModule, AZ::SystemAllocator);

        ServerBrowserModule()
        {
            m_descriptors.insert(m_descriptors.end(), {
                ServerBrowserSystemComponent::CreateDescriptor(),
            });
        }

        AZ::ComponentTypeList GetRequiredSystemComponents() const override
        {
            return AZ::ComponentTypeList{ azrtti_typeid<ServerBrowserSystemComponent>() };
        }
    };
} // namespace ServerBrowser

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME), ServerBrowser::ServerBrowserModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_ServerBrowser, ServerBrowser::ServerBrowserModule)
#endif
