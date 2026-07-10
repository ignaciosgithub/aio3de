/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Module/Module.h>

#include "EditorLevelStreamingComponent.h"
#include "LevelStreamingComponent.h"

namespace LevelStreaming
{
    class LevelStreamingEditorModule
        : public AZ::Module
    {
    public:
        AZ_RTTI(LevelStreamingEditorModule, "{5A6B7C8D-9E0F-1A2B-3C4D-5E6F7A8B9C0D}", AZ::Module);
        AZ_CLASS_ALLOCATOR(LevelStreamingEditorModule, AZ::SystemAllocator);

        LevelStreamingEditorModule()
        {
            m_descriptors.insert(
                m_descriptors.end(),
                {
                    LevelStreamingComponent::CreateDescriptor(),
                    EditorLevelStreamingComponent::CreateDescriptor(),
                });
        }
    };
} // namespace LevelStreaming

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME, _Editor), LevelStreaming::LevelStreamingEditorModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_LevelStreaming_Editor, LevelStreaming::LevelStreamingEditorModule)
#endif
