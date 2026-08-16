/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Module/Module.h>

#include <Clients/VideoTextureComponent.h>
#include <Clients/VideoTextureSystemComponent.h>
#include <Tools/EditorVideoTextureComponent.h>
#include <Tools/VideoTextureEditorSystemComponent.h>

namespace VideoTexture
{
    class VideoTextureEditorModule
        : public AZ::Module
    {
    public:
        AZ_RTTI(VideoTextureEditorModule, "{8A0D2E5B-11C4-4F6E-9A37-52C0D6B4E981}", AZ::Module);
        AZ_CLASS_ALLOCATOR(VideoTextureEditorModule, AZ::SystemAllocator);

        VideoTextureEditorModule()
        {
            m_descriptors.insert(
                m_descriptors.end(),
                {
                    VideoTextureSystemComponent::CreateDescriptor(),
                    VideoTextureEditorSystemComponent::CreateDescriptor(),
                    VideoTextureComponent::CreateDescriptor(),
                    EditorVideoTextureComponent::CreateDescriptor(),
                });
        }

        AZ::ComponentTypeList GetRequiredSystemComponents() const override
        {
            return AZ::ComponentTypeList{ azrtti_typeid<VideoTextureEditorSystemComponent>() };
        }
    };
} // namespace VideoTexture

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME), VideoTexture::VideoTextureEditorModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_VideoTexture_Editor, VideoTexture::VideoTextureEditorModule)
#endif
