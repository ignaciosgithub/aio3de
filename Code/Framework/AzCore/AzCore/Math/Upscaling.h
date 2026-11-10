/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/base.h>
#include <AzCore/std/utility/pair.h>

namespace AZ
{
    enum class UpscaleQuality : uint32_t
    {
        UltraQuality = 0,
        Quality = 1,
        Balanced = 2,
        Performance = 3
    };

    AZCORE_API float GetUpscaleRatio(UpscaleQuality quality);
    AZCORE_API AZStd::pair<uint32_t, uint32_t> ComputeUpscaleRenderSize(
        uint32_t outputWidth, uint32_t outputHeight, UpscaleQuality quality);

    struct FsrEasuConstants
    {
        uint32_t m_con0[4];
        uint32_t m_con1[4];
        uint32_t m_con2[4];
        uint32_t m_con3[4];
    };

    AZCORE_API FsrEasuConstants ComputeFsrEasuConstants(
        float viewportWidth,
        float viewportHeight,
        float inputWidth,
        float inputHeight,
        float outputWidth,
        float outputHeight);

    struct FsrRcasConstants
    {
        uint32_t m_con[4];
    };

    AZCORE_API FsrRcasConstants ComputeFsrRcasConstants(float sharpnessStops);
}
