/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Module/Module.h>

#include "VideoTextureComponent.h"
#include "VideoTextureSystemComponent.h"

namespace VideoTexture
{
    class VideoTextureModule
        : public AZ::Module
    {
    public:
        AZ_RTTI(VideoTextureModule, "{EF07A2F6-1E07-4292-BB68-F66749535D6C}", AZ::Module);
        AZ_CLASS_ALLOCATOR(VideoTextureModule, AZ::SystemAllocator);

        VideoTextureModule()
        {
            m_descriptors.insert(
                m_descriptors.end(),
                {
                    VideoTextureSystemComponent::CreateDescriptor(),
                    VideoTextureComponent::CreateDescriptor(),
                });
        }

        AZ::ComponentTypeList GetRequiredSystemComponents() const override
        {
            return AZ::ComponentTypeList{ azrtti_typeid<VideoTextureSystemComponent>() };
        }
    };
} // namespace VideoTexture

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME), VideoTexture::VideoTextureModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_VideoTexture, VideoTexture::VideoTextureModule)
#endif
