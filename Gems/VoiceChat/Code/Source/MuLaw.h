/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzCore/base.h>

namespace VoiceChat
{
    //! G.711 mu-law: 16-bit PCM <-> 8 bits per sample (2:1 over raw at the
    //! same rate; telephony-grade quality, no patents, no dependencies).
    namespace MuLaw
    {
        inline AZ::u8 Encode(AZ::s16 sample)
        {
            constexpr AZ::s16 Bias = 0x84;
            constexpr AZ::s16 Clip = 32635;

            const AZ::u8 sign = (sample < 0) ? 0x80 : 0;
            AZ::s32 magnitude = sign ? -static_cast<AZ::s32>(sample) : sample;
            if (magnitude > Clip)
            {
                magnitude = Clip;
            }
            magnitude += Bias;

            AZ::u8 exponent = 7;
            for (AZ::s32 mask = 0x4000; (magnitude & mask) == 0 && exponent > 0; mask >>= 1)
            {
                --exponent;
            }
            const AZ::u8 mantissa = static_cast<AZ::u8>((magnitude >> (exponent + 3)) & 0x0F);
            return ~(sign | (exponent << 4) | mantissa);
        }

        inline AZ::s16 Decode(AZ::u8 encoded)
        {
            encoded = ~encoded;
            const AZ::u8 sign = encoded & 0x80;
            const AZ::u8 exponent = (encoded >> 4) & 0x07;
            const AZ::u8 mantissa = encoded & 0x0F;
            AZ::s32 magnitude = ((static_cast<AZ::s32>(mantissa) << 3) + 0x84) << exponent;
            magnitude -= 0x84;
            return static_cast<AZ::s16>(sign ? -magnitude : magnitude);
        }
    } // namespace MuLaw
} // namespace VoiceChat
