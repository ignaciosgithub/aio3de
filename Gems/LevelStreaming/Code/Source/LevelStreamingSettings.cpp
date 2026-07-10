/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "LevelStreamingSettings.h"

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace LevelStreaming
{
    void LevelStreamingSettings::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<LevelStreamingSettings>()
                ->Version(1)
                ->Field("ChunkSize", &LevelStreamingSettings::m_chunkSize)
                ->Field("StreamDistance", &LevelStreamingSettings::m_streamDistance)
                ->Field("Hysteresis", &LevelStreamingSettings::m_hysteresis)
                ->Field("RebuildInterval", &LevelStreamingSettings::m_rebuildInterval);

            if (auto* editContext = serializeContext->GetEditContext())
            {
                editContext->Class<LevelStreamingSettings>("Level Streaming Settings", "Streamable chunk grid settings")
                    ->DataElement(AZ::Edit::UIHandlers::Slider, &LevelStreamingSettings::m_chunkSize,
                        "Chunk size", "Horizontal (XY) size of a streamable chunk in meters. Chunk bounds grow vertically to the tallest object in each chunk, so tall objects keep streaming from further away.")
                        ->Attribute(AZ::Edit::Attributes::Min, 4.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 4096.0f)
                        ->Attribute(AZ::Edit::Attributes::SoftMax, 512.0f)
                    ->DataElement(AZ::Edit::UIHandlers::Slider, &LevelStreamingSettings::m_streamDistance,
                        "Stream distance", "Distance from the camera to a chunk's bounds under which the chunk streams in.")
                        ->Attribute(AZ::Edit::Attributes::Min, 8.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 100000.0f)
                        ->Attribute(AZ::Edit::Attributes::SoftMax, 4096.0f)
                    ->DataElement(AZ::Edit::UIHandlers::Slider, &LevelStreamingSettings::m_hysteresis,
                        "Hysteresis", "Extra fraction of the stream distance a chunk must exceed before streaming back out, preventing flicker at the boundary.")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 1.0f)
                    ->DataElement(AZ::Edit::UIHandlers::Slider, &LevelStreamingSettings::m_rebuildInterval,
                        "Rebuild interval", "Seconds between chunk-membership rebuilds (picks up moved, spawned, and removed objects).")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.05f)
                        ->Attribute(AZ::Edit::Attributes::Max, 30.0f)
                        ->Attribute(AZ::Edit::Attributes::SoftMax, 5.0f);
            }
        }
    }
} // namespace LevelStreaming
