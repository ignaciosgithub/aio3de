/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "VideoTextureSystemComponent.h"

#include <AzCore/Serialization/SerializeContext.h>
#include <VideoTexture/VideoAsset.h>

namespace VideoTexture
{
    void VideoTextureSystemComponent::Reflect(AZ::ReflectContext* context)
    {
        VideoAsset::Reflect(context);

        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<VideoTextureSystemComponent, AZ::Component>()->Version(1);
        }
    }

    void VideoTextureSystemComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("VideoTextureSystemService"));
    }

    void VideoTextureSystemComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("VideoTextureSystemService"));
    }

    void VideoTextureSystemComponent::Activate()
    {
        m_videoAssetHandler = AZStd::make_unique<VideoAssetHandler>();
    }

    void VideoTextureSystemComponent::Deactivate()
    {
        m_videoAssetHandler.reset();
    }
} // namespace VideoTexture
