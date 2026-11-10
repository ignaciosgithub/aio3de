/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <PostProcessing/FsrUpscalePasses.h>

#include <AzCore/Console/Console.h>
#include <AzCore/Math/Upscaling.h>
#include <AzCore/std/containers/array.h>
#include <Atom/RPI.Public/Pass/PassAttachment.h>
#include <Atom/RPI.Public/RenderPipeline.h>

namespace AZ::Render
{
    AZ_CVAR(uint32_t, r_upscaler, 0, nullptr, ConsoleFunctorFlags::Null, "Select the spatial upscaler (0=off, 1=FSR 1).");
    AZ_CVAR(uint32_t, r_upscaleQuality, 1, nullptr, ConsoleFunctorFlags::Null, "FSR 1 quality mode.");
    AZ_CVAR(float, r_upscaleSharpness, 0.2f, nullptr, ConsoleFunctorFlags::Null, "FSR 1 RCAS sharpness in stops.");

    namespace
    {
        AZStd::array<uint32_t, 4> ToArray(const uint32_t (&values)[4])
        {
            return { values[0], values[1], values[2], values[3] };
        }
    }

    RPI::Ptr<FsrUpscaleParentPass> FsrUpscaleParentPass::Create(const RPI::PassDescriptor& descriptor)
    {
        return aznew FsrUpscaleParentPass(descriptor);
    }

    FsrUpscaleParentPass::FsrUpscaleParentPass(const RPI::PassDescriptor& descriptor)
        : ParentPass(descriptor)
    {
    }

    bool FsrUpscaleParentPass::IsEnabled() const
    {
        const uint32_t upscaler = r_upscaler;
        if (upscaler >= 2)
        {
            static bool warnedReservedUpscaler = false;
            if (!warnedReservedUpscaler)
            {
                AZ_Warning("Upscaling", false, "r_upscaler value %u is reserved; upscaling is disabled.", upscaler);
                warnedReservedUpscaler = true;
            }
        }

        const bool enabled = ParentPass::IsEnabled() && upscaler == 1;
        const auto quality = static_cast<UpscaleQuality>(static_cast<uint32_t>(r_upscaleQuality));
        const float scale = enabled
            ? 1.0f / GetUpscaleRatio(quality)
            : 1.0f;
        if (RPI::RenderPipeline* pipeline = GetRenderPipeline())
        {
            pipeline->SetRenderResolutionScale(scale);
        }
        return enabled;
    }

    RPI::Ptr<FsrEasuPass> FsrEasuPass::Create(const RPI::PassDescriptor& descriptor)
    {
        return aznew FsrEasuPass(descriptor);
    }

    FsrEasuPass::FsrEasuPass(const RPI::PassDescriptor& descriptor)
        : ComputePass(descriptor)
    {
    }

    void FsrEasuPass::FrameBeginInternal(FramePrepareParams params)
    {
        if (m_shaderResourceGroup && GetInputCount() > 0 && GetOutputCount() > 0)
        {
            const auto input = GetInputBinding(0).GetAttachment();
            const auto output = GetOutputBinding(0).GetAttachment();
            if (input && output &&
                input->GetAttachmentType() == RHI::AttachmentType::Image &&
                output->GetAttachmentType() == RHI::AttachmentType::Image)
            {
                const RHI::Size& inputSize = input->m_descriptor.m_image.m_size;
                const RHI::Size& outputSize = output->m_descriptor.m_image.m_size;
                const FsrEasuConstants constants = ComputeFsrEasuConstants(
                    static_cast<float>(inputSize.m_width),
                    static_cast<float>(inputSize.m_height),
                    static_cast<float>(inputSize.m_width),
                    static_cast<float>(inputSize.m_height),
                    static_cast<float>(outputSize.m_width),
                    static_cast<float>(outputSize.m_height));
                m_shaderResourceGroup->SetConstant(m_con0Index, ToArray(constants.m_con0));
                m_shaderResourceGroup->SetConstant(m_con1Index, ToArray(constants.m_con1));
                m_shaderResourceGroup->SetConstant(m_con2Index, ToArray(constants.m_con2));
                m_shaderResourceGroup->SetConstant(m_con3Index, ToArray(constants.m_con3));
            }
        }
        ComputePass::FrameBeginInternal(params);
    }

    RPI::Ptr<FsrRcasPass> FsrRcasPass::Create(const RPI::PassDescriptor& descriptor)
    {
        return aznew FsrRcasPass(descriptor);
    }

    FsrRcasPass::FsrRcasPass(const RPI::PassDescriptor& descriptor)
        : ComputePass(descriptor)
    {
    }

    void FsrRcasPass::FrameBeginInternal(FramePrepareParams params)
    {
        if (m_shaderResourceGroup)
        {
            const FsrRcasConstants constants = ComputeFsrRcasConstants(r_upscaleSharpness);
            m_shaderResourceGroup->SetConstant(m_constantsIndex, ToArray(constants.m_con));
        }
        ComputePass::FrameBeginInternal(params);
    }
} // namespace AZ::Render
