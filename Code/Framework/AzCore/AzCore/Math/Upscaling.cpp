/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/*
 * FidelityFX Super Resolution Sample
 *
 * Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Copyright (c) 2014 Michal Drobot (for concepts used in "FLOAT APPROXIMATIONS").
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <AzCore/Math/Upscaling.h>
#include <AzCore/std/algorithm.h>

#include <bit>
#include <cmath>

namespace AZ
{
    namespace
    {
        uint32_t FloatToHalfBits(float value)
        {
            const uint32_t bits = std::bit_cast<uint32_t>(value);
            const uint32_t sign = (bits >> 16) & 0x8000;
            const uint32_t exponent = (bits >> 23) & 0xff;
            uint32_t mantissa = bits & 0x7fffff;

            if (exponent == 0xff)
            {
                return sign | 0x7c00 | (mantissa != 0 ? 0x0200 : 0);
            }

            const int32_t halfExponent = static_cast<int32_t>(exponent) - 127 + 15;
            if (halfExponent >= 31)
            {
                return sign | 0x7c00;
            }

            if (halfExponent <= 0)
            {
                if (halfExponent < -10)
                {
                    return sign;
                }

                mantissa |= 0x800000;
                const uint32_t shift = static_cast<uint32_t>(14 - halfExponent);
                uint32_t halfMantissa = mantissa >> shift;
                const uint32_t remainder = mantissa & ((1u << shift) - 1);
                const uint32_t halfway = 1u << (shift - 1);
                if (remainder > halfway || (remainder == halfway && (halfMantissa & 1)))
                {
                    ++halfMantissa;
                }
                return sign | halfMantissa;
            }

            uint32_t halfMantissa = mantissa >> 13;
            const uint32_t remainder = mantissa & 0x1fff;
            if (remainder > 0x1000 || (remainder == 0x1000 && (halfMantissa & 1)))
            {
                ++halfMantissa;
            }

            uint32_t roundedExponent = static_cast<uint32_t>(halfExponent);
            if (halfMantissa == 0x400)
            {
                halfMantissa = 0;
                ++roundedExponent;
            }

            if (roundedExponent >= 31)
            {
                return sign | 0x7c00;
            }

            return sign | (roundedExponent << 10) | halfMantissa;
        }

        uint32_t FloatBits(float value)
        {
            return std::bit_cast<uint32_t>(value);
        }
    }

    float GetUpscaleRatio(UpscaleQuality quality)
    {
        switch (quality)
        {
        case UpscaleQuality::UltraQuality:
            return 1.3f;
        case UpscaleQuality::Quality:
            return 1.5f;
        case UpscaleQuality::Balanced:
            return 1.7f;
        case UpscaleQuality::Performance:
        default:
            return 2.0f;
        }
    }

    AZStd::pair<uint32_t, uint32_t> ComputeUpscaleRenderSize(
        uint32_t outputWidth, uint32_t outputHeight, UpscaleQuality quality)
    {
        const double ratio = GetUpscaleRatio(quality);
        const auto dimension = [ratio](uint32_t outputSize)
        {
            return AZStd::max(1u, static_cast<uint32_t>(std::round(static_cast<double>(outputSize) / ratio)));
        };
        return { dimension(outputWidth), dimension(outputHeight) };
    }

    FsrEasuConstants ComputeFsrEasuConstants(
        float viewportWidth,
        float viewportHeight,
        float inputWidth,
        float inputHeight,
        float outputWidth,
        float outputHeight)
    {
        FsrEasuConstants constants{};
        const float reciprocalOutputWidth = 1.0f / outputWidth;
        const float reciprocalOutputHeight = 1.0f / outputHeight;
        const float reciprocalInputWidth = 1.0f / inputWidth;
        const float reciprocalInputHeight = 1.0f / inputHeight;

        constants.m_con0[0] = FloatBits(viewportWidth * reciprocalOutputWidth);
        constants.m_con0[1] = FloatBits(viewportHeight * reciprocalOutputHeight);
        constants.m_con0[2] = FloatBits(0.5f * viewportWidth * reciprocalOutputWidth - 0.5f);
        constants.m_con0[3] = FloatBits(0.5f * viewportHeight * reciprocalOutputHeight - 0.5f);

        constants.m_con1[0] = FloatBits(reciprocalInputWidth);
        constants.m_con1[1] = FloatBits(reciprocalInputHeight);
        constants.m_con1[2] = FloatBits(reciprocalInputWidth);
        constants.m_con1[3] = FloatBits(-reciprocalInputHeight);

        constants.m_con2[0] = FloatBits(-reciprocalInputWidth);
        constants.m_con2[1] = FloatBits(2.0f * reciprocalInputHeight);
        constants.m_con2[2] = FloatBits(reciprocalInputWidth);
        constants.m_con2[3] = FloatBits(2.0f * reciprocalInputHeight);

        constants.m_con3[0] = FloatBits(0.0f);
        constants.m_con3[1] = FloatBits(4.0f * reciprocalInputHeight);
        constants.m_con3[2] = 0;
        constants.m_con3[3] = 0;
        return constants;
    }

    FsrRcasConstants ComputeFsrRcasConstants(float sharpnessStops)
    {
        FsrRcasConstants constants{};
        const float sharpness = std::exp2(-sharpnessStops);
        const uint32_t packedHalf = FloatToHalfBits(sharpness);
        constants.m_con[0] = FloatBits(sharpness);
        constants.m_con[1] = packedHalf | (packedHalf << 16);
        return constants;
    }
}
