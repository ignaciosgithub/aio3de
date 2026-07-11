/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/RTTI/RTTI.h>
#include <AzCore/Memory/SystemAllocator.h>

namespace AZ
{
    class ReflectContext;
}

namespace LevelStreaming
{
    //! Tunables for the level streaming grid, editable in the entity inspector.
    struct LevelStreamingSettings
    {
        AZ_TYPE_INFO(LevelStreamingSettings, "{4C1A9E2B-7D3F-4B6A-8E5C-0F1D2A3B4C5D}");
        AZ_CLASS_ALLOCATOR(LevelStreamingSettings, AZ::SystemAllocator);

        static void Reflect(AZ::ReflectContext* context);

        //! Horizontal (XY) size of a streamable chunk in meters. Chunk bounds grow vertically to the
        //! tallest object inside the chunk, so tall objects keep streaming from further away.
        float m_chunkSize = 64.0f;

        //! Distance from the camera to a chunk's bounds under which the chunk streams in.
        float m_streamDistance = 256.0f;

        //! Extra fraction of the stream distance a chunk must exceed before streaming back out,
        //! preventing flicker at the boundary.
        float m_hysteresis = 0.1f;

        //! Seconds between chunk-membership rebuilds (picks up moved/spawned/removed objects).
        float m_rebuildInterval = 0.5f;
    };
} // namespace LevelStreaming
