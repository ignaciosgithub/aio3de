/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Module/Module.h>

#include "EditorSoftBodyComponent.h"
#include "SoftBodyComponent.h"

namespace SoftBodyPhysics
{
    class SoftBodyPhysicsEditorModule
        : public AZ::Module
    {
    public:
        AZ_RTTI(SoftBodyPhysicsEditorModule, "{7A0B1C2D-3E4F-5A6B-7C8D-9E0F1A2B3C4D}", AZ::Module);
        AZ_CLASS_ALLOCATOR(SoftBodyPhysicsEditorModule, AZ::SystemAllocator);

        SoftBodyPhysicsEditorModule()
        {
            m_descriptors.insert(
                m_descriptors.end(),
                {
                    SoftBodyComponent::CreateDescriptor(),
                    EditorSoftBodyComponent::CreateDescriptor(),
                });
        }
    };
} // namespace SoftBodyPhysics

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME, _Editor), SoftBodyPhysics::SoftBodyPhysicsEditorModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_SoftBodyPhysics_Editor, SoftBodyPhysics::SoftBodyPhysicsEditorModule)
#endif
