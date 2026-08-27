/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Module/Module.h>

#include "CSharpScriptComponent.h"
#include "EditorCSharpScriptComponent.h"

namespace CSharpScripting
{
    class CSharpScriptingEditorModule
        : public AZ::Module
    {
    public:
        AZ_RTTI(CSharpScriptingEditorModule, "{5A9B3C7D-2E4F-46A8-B0C1-D2E3F4A5B6C7}", AZ::Module);
        AZ_CLASS_ALLOCATOR(CSharpScriptingEditorModule, AZ::SystemAllocator);

        CSharpScriptingEditorModule()
        {
            m_descriptors.insert(
                m_descriptors.end(),
                {
                    CSharpScriptComponent::CreateDescriptor(),
                    EditorCSharpScriptComponent::CreateDescriptor(),
                });
        }
    };
} // namespace CSharpScripting

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME, _Editor), CSharpScripting::CSharpScriptingEditorModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_CSharpScripting_Editor, CSharpScripting::CSharpScriptingEditorModule)
#endif
