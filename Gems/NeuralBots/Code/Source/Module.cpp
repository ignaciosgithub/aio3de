/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include <AzCore/Module/Module.h>
#include <AzCore/RTTI/RTTI.h>

#include "BotAgentComponent.h"

namespace NeuralBots
{
    class NeuralBotsModule
        : public AZ::Module
    {
    public:
        AZ_RTTI(NeuralBotsModule, "{B4E2A81C-6F3D-49A5-9C70-1D8E5B2F4A67}", AZ::Module);
        AZ_CLASS_ALLOCATOR(NeuralBotsModule, AZ::SystemAllocator);

        NeuralBotsModule()
        {
            m_descriptors.insert(m_descriptors.end(), {
                BotAgentComponent::CreateDescriptor(),
            });
        }
    };
} // namespace NeuralBots

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME), NeuralBots::NeuralBotsModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_NeuralBots, NeuralBots::NeuralBotsModule)
#endif
