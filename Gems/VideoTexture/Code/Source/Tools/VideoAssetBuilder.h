/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AssetBuilderSDK/AssetBuilderBusses.h>

namespace VideoTexture
{
    //! Packages source .mpg/.mpeg MPEG-1 files into VideoAsset products.
    class VideoAssetBuilder
        : public AssetBuilderSDK::AssetBuilderCommandBus::Handler
    {
    public:
        AZ_RTTI(VideoAssetBuilder, "{2F19CFA3-BA1A-4589-884D-744C9435BF58}");

        VideoAssetBuilder() = default;

        //! AssetBuilderCommandBus overrides ...
        void CreateJobs(const AssetBuilderSDK::CreateJobsRequest& request, AssetBuilderSDK::CreateJobsResponse& response) const;
        void ProcessJob(const AssetBuilderSDK::ProcessJobRequest& request, AssetBuilderSDK::ProcessJobResponse& response) const;
        void ShutDown() override {}
    };
} // namespace VideoTexture
