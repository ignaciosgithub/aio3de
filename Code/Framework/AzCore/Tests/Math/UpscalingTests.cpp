/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Math/Upscaling.h>
#include <AzCore/UnitTest/TestTypes.h>

namespace UnitTest
{
    TEST(Upscaling, QualityRatios)
    {
        EXPECT_FLOAT_EQ(1.3f, AZ::GetUpscaleRatio(AZ::UpscaleQuality::UltraQuality));
        EXPECT_FLOAT_EQ(1.5f, AZ::GetUpscaleRatio(AZ::UpscaleQuality::Quality));
        EXPECT_FLOAT_EQ(1.7f, AZ::GetUpscaleRatio(AZ::UpscaleQuality::Balanced));
        EXPECT_FLOAT_EQ(2.0f, AZ::GetUpscaleRatio(AZ::UpscaleQuality::Performance));
        EXPECT_FLOAT_EQ(2.0f, AZ::GetUpscaleRatio(static_cast<AZ::UpscaleQuality>(4)));
    }

    TEST(Upscaling, RenderSizesForHDAnd4K)
    {
        struct ExpectedSizes
        {
            AZ::UpscaleQuality m_quality;
            AZStd::pair<uint32_t, uint32_t> m_hd;
            AZStd::pair<uint32_t, uint32_t> m_4k;
        };

        const ExpectedSizes expected[] = {
            { AZ::UpscaleQuality::UltraQuality, { 1477, 831 }, { 2954, 1662 } },
            { AZ::UpscaleQuality::Quality, { 1280, 720 }, { 2560, 1440 } },
            { AZ::UpscaleQuality::Balanced, { 1129, 635 }, { 2259, 1271 } },
            { AZ::UpscaleQuality::Performance, { 960, 540 }, { 1920, 1080 } }
        };

        for (const ExpectedSizes& size : expected)
        {
            EXPECT_EQ(size.m_hd, AZ::ComputeUpscaleRenderSize(1920, 1080, size.m_quality));
            EXPECT_EQ(size.m_4k, AZ::ComputeUpscaleRenderSize(3840, 2160, size.m_quality));
            const auto minimumSize = AZ::ComputeUpscaleRenderSize(1, 1, size.m_quality);
            EXPECT_EQ(1u, minimumSize.first);
            EXPECT_EQ(1u, minimumSize.second);
        }
    }

    TEST(Upscaling, EasuConstantsMatchFsrSetup)
    {
        const AZ::FsrEasuConstants constants = AZ::ComputeFsrEasuConstants(1280.0f, 720.0f, 1280.0f, 720.0f, 1920.0f, 1080.0f);
        const uint32_t expectedCon0[] = { 0x3f2aaaab, 0x3f2aaaaa, 0xbe2aaaaa, 0xbe2aaaac };
        const uint32_t expectedCon1[] = { 0x3a4ccccd, 0x3ab60b61, 0x3a4ccccd, 0xbab60b61 };
        const uint32_t expectedCon2[] = { 0xba4ccccd, 0x3b360b61, 0x3a4ccccd, 0x3b360b61 };
        const uint32_t expectedCon3[] = { 0, 0x3bb60b61, 0, 0 };

        for (uint32_t index = 0; index < 4; ++index)
        {
            EXPECT_EQ(expectedCon0[index], constants.m_con0[index]);
            EXPECT_EQ(expectedCon1[index], constants.m_con1[index]);
            EXPECT_EQ(expectedCon2[index], constants.m_con2[index]);
            EXPECT_EQ(expectedCon3[index], constants.m_con3[index]);
        }
    }

    TEST(Upscaling, RcasConstantsMatchFsrSetup)
    {
        const AZ::FsrRcasConstants maximumSharpness = AZ::ComputeFsrRcasConstants(0.0f);
        EXPECT_EQ(0x3f800000, maximumSharpness.m_con[0]);
        EXPECT_EQ(0x3c003c00, maximumSharpness.m_con[1]);
        EXPECT_EQ(0, maximumSharpness.m_con[2]);
        EXPECT_EQ(0, maximumSharpness.m_con[3]);

        const AZ::FsrRcasConstants oneStopReduction = AZ::ComputeFsrRcasConstants(1.0f);
        EXPECT_EQ(0x3f000000, oneStopReduction.m_con[0]);
        EXPECT_EQ(0x38003800, oneStopReduction.m_con[1]);
        EXPECT_EQ(0, oneStopReduction.m_con[2]);
        EXPECT_EQ(0, oneStopReduction.m_con[3]);
    }
}
