/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <VideoTexture/VideoAsset.h>

#include <AzCore/Serialization/SerializeContext.h>

namespace VideoTexture
{
    void VideoAsset::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<VideoAsset, AZ::Data::AssetData>()
                ->Version(1)
                ->Field("Data", &VideoAsset::m_data);
        }
    }
} // namespace VideoTexture
