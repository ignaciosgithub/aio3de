/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "VideoAssetHandler.h"

#include <AzCore/Asset/AssetSerializer.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Serialization/Utils.h>
#include <VideoTexture/VideoAsset.h>

namespace VideoTexture
{
    VideoAssetHandler::VideoAssetHandler()
    {
        Register();
    }

    VideoAssetHandler::~VideoAssetHandler()
    {
        Unregister();
    }

    void VideoAssetHandler::Register()
    {
        const bool assetManagerReady = AZ::Data::AssetManager::IsReady();
        AZ_Error("VideoAssetHandler", assetManagerReady, "Asset manager isn't ready.");
        if (assetManagerReady)
        {
            AZ::Data::AssetManager::Instance().RegisterHandler(this, AZ::AzTypeInfo<VideoAsset>::Uuid());
        }

        AZ::AssetTypeInfoBus::Handler::BusConnect(AZ::AzTypeInfo<VideoAsset>::Uuid());
    }

    void VideoAssetHandler::Unregister()
    {
        AZ::AssetTypeInfoBus::Handler::BusDisconnect();

        if (AZ::Data::AssetManager::IsReady())
        {
            AZ::Data::AssetManager::Instance().UnregisterHandler(this);
        }
    }

    AZ::Data::AssetType VideoAssetHandler::GetAssetType() const
    {
        return AZ::AzTypeInfo<VideoAsset>::Uuid();
    }

    void VideoAssetHandler::GetAssetTypeExtensions(AZStd::vector<AZStd::string>& extensions)
    {
        extensions.push_back(VideoAsset::FileExtension);
    }

    const char* VideoAssetHandler::GetAssetTypeDisplayName() const
    {
        return "Video Asset (VideoTexture Gem)";
    }

    const char* VideoAssetHandler::GetBrowserIcon() const
    {
        return "Icons/Components/ColliderMesh.svg";
    }

    const char* VideoAssetHandler::GetGroup() const
    {
        return VideoAsset::AssetGroup;
    }

    AZ::Uuid VideoAssetHandler::GetComponentTypeId() const
    {
        return AZ::Uuid::CreateNull();
    }

    bool VideoAssetHandler::CanCreateComponent([[maybe_unused]] const AZ::Data::AssetId& assetId) const
    {
        return false;
    }

    AZ::Data::AssetPtr VideoAssetHandler::CreateAsset([[maybe_unused]] const AZ::Data::AssetId& id, const AZ::Data::AssetType& type)
    {
        if (type == AZ::AzTypeInfo<VideoAsset>::Uuid())
        {
            return aznew VideoAsset();
        }

        AZ_Error("VideoAssetHandler", false, "This handler deals only with VideoAsset type.");
        return nullptr;
    }

    AZ::Data::AssetHandler::LoadResult VideoAssetHandler::LoadAssetData(
        const AZ::Data::Asset<AZ::Data::AssetData>& asset,
        AZStd::shared_ptr<AZ::Data::AssetDataStream> stream,
        [[maybe_unused]] const AZ::Data::AssetFilterCB& assetLoadFilterCB)
    {
        const bool result = AZ::Utils::LoadObjectFromStreamInPlace<VideoAsset>(*stream, *asset.GetAs<VideoAsset>());
        if (!result)
        {
            AZ_Error("VideoAssetHandler", false, "Failed to load video asset");
            return AssetHandler::LoadResult::Error;
        }

        return AssetHandler::LoadResult::LoadComplete;
    }

    void VideoAssetHandler::DestroyAsset(AZ::Data::AssetPtr ptr)
    {
        delete ptr;
    }

    void VideoAssetHandler::GetHandledAssetTypes(AZStd::vector<AZ::Data::AssetType>& assetTypes)
    {
        assetTypes.push_back(AZ::AzTypeInfo<VideoAsset>::Uuid());
    }
} // namespace VideoTexture
