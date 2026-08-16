/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/Asset/AssetSerializer.h>
#include <AzCore/std/containers/vector.h>

namespace VideoTexture
{
    //! Holds the raw bytes of an MPEG-1 video file (.mpg / MPEG-PS container).
    class VideoAsset
        : public AZ::Data::AssetData
    {
    public:
        AZ_CLASS_ALLOCATOR(VideoAsset, AZ::SystemAllocator, 0);
        AZ_RTTI(VideoAsset, "{26DE660D-08C7-488A-AA11-2EB477ED7E42}", AZ::Data::AssetData);

        static constexpr const char* FileExtension = "videotexture";
        static constexpr const char* AssetGroup = "Video";
        static constexpr AZ::u32 AssetSubId = 1;

        static void Reflect(AZ::ReflectContext* context);

        VideoAsset() = default;
        ~VideoAsset() override = default;

        AZStd::vector<AZ::u8> m_data;
    };
} // namespace VideoTexture
