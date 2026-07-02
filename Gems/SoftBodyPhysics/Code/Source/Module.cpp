/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Module/Module.h>

#include "SoftBodyComponent.h"

namespace SoftBodyPhysics
{
    class SoftBodyPhysicsModule
        : public AZ::Module
    {
    public:
        AZ_RTTI(SoftBodyPhysicsModule, "{2E6F1B7A-8C3D-4A5E-9F01-B2C3D4E5F6A7}", AZ::Module);
        AZ_CLASS_ALLOCATOR(SoftBodyPhysicsModule, AZ::SystemAllocator);

        SoftBodyPhysicsModule()
        {
            m_descriptors.insert(
                m_descriptors.end(),
                {
                    SoftBodyComponent::CreateDescriptor(),
                });
        }
    };
} // namespace SoftBodyPhysics

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME), SoftBodyPhysics::SoftBodyPhysicsModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_SoftBodyPhysics, SoftBodyPhysics::SoftBodyPhysicsModule)
#endif
