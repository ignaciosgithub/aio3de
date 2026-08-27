/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Module/Module.h>

#include "CSharpScriptComponent.h"

namespace CSharpScripting
{
    class CSharpScriptingModule
        : public AZ::Module
    {
    public:
        AZ_RTTI(CSharpScriptingModule, "{4F8A2B6C-1D3E-45F7-A9B0-C1D2E3F4A5B6}", AZ::Module);
        AZ_CLASS_ALLOCATOR(CSharpScriptingModule, AZ::SystemAllocator);

        CSharpScriptingModule()
        {
            m_descriptors.insert(
                m_descriptors.end(),
                {
                    CSharpScriptComponent::CreateDescriptor(),
                });
        }
    };
} // namespace CSharpScripting

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME), CSharpScripting::CSharpScriptingModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_CSharpScripting, CSharpScripting::CSharpScriptingModule)
#endif
