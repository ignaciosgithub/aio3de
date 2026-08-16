/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "VideoTextureEditorSystemComponent.h"

#include <AssetBuilderSDK/AssetBuilderSDK.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace VideoTexture
{
    void VideoTextureEditorSystemComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<VideoTextureEditorSystemComponent, VideoTextureSystemComponent>()
                ->Version(1)
                ->Attribute(AZ::Edit::Attributes::SystemComponentTags, AZStd::vector<AZ::Crc32>({ AZ_CRC_CE("AssetBuilder") }));
        }
    }

    void VideoTextureEditorSystemComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        BaseSystemComponent::GetProvidedServices(provided);
        provided.push_back(AZ_CRC_CE("VideoTextureEditorService"));
    }

    void VideoTextureEditorSystemComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        BaseSystemComponent::GetIncompatibleServices(incompatible);
        incompatible.push_back(AZ_CRC_CE("VideoTextureEditorService"));
    }

    void VideoTextureEditorSystemComponent::GetDependentServices(AZ::ComponentDescriptor::DependencyArrayType& dependent)
    {
        dependent.push_back(AZ_CRC_CE("AssetDatabaseService"));
        dependent.push_back(AZ_CRC_CE("AssetCatalogService"));
    }

    void VideoTextureEditorSystemComponent::Activate()
    {
        BaseSystemComponent::Activate();

        AssetBuilderSDK::AssetBuilderDesc videoAssetBuilderDescriptor;
        videoAssetBuilderDescriptor.m_name = "Video Texture Asset Builder";
        videoAssetBuilderDescriptor.m_version = 1; // bump this to rebuild all video files
        videoAssetBuilderDescriptor.m_patterns.push_back(
            AssetBuilderSDK::AssetBuilderPattern("*.mpg", AssetBuilderSDK::AssetBuilderPattern::PatternType::Wildcard));
        videoAssetBuilderDescriptor.m_patterns.push_back(
            AssetBuilderSDK::AssetBuilderPattern("*.mpeg", AssetBuilderSDK::AssetBuilderPattern::PatternType::Wildcard));
        videoAssetBuilderDescriptor.m_busId = azrtti_typeid<VideoAssetBuilder>();
        videoAssetBuilderDescriptor.m_createJobFunction =
            [this](const AssetBuilderSDK::CreateJobsRequest& request, AssetBuilderSDK::CreateJobsResponse& response)
        {
            m_videoAssetBuilder.CreateJobs(request, response);
        };
        videoAssetBuilderDescriptor.m_processJobFunction =
            [this](const AssetBuilderSDK::ProcessJobRequest& request, AssetBuilderSDK::ProcessJobResponse& response)
        {
            m_videoAssetBuilder.ProcessJob(request, response);
        };
        m_videoAssetBuilder.BusConnect(videoAssetBuilderDescriptor.m_busId);
        AssetBuilderSDK::AssetBuilderBus::Broadcast(
            &AssetBuilderSDK::AssetBuilderBus::Handler::RegisterBuilderInformation, videoAssetBuilderDescriptor);
    }

    void VideoTextureEditorSystemComponent::Deactivate()
    {
        m_videoAssetBuilder.BusDisconnect();
        BaseSystemComponent::Deactivate();
    }
} // namespace VideoTexture
