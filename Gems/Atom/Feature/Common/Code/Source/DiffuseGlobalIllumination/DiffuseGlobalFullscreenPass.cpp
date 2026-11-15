/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include <DiffuseGlobalIllumination/DiffuseGlobalFullscreenPass.h>

#include <RayTracing/PortableGiUpdatePass.h>

#include <Atom/RHI.Reflect/BufferDescriptor.h>
#include <Atom/RHI/FrameGraphInterface.h>
#include <Atom/RPI.Public/Pass/PassFilter.h>
#include <Atom/RPI.Public/Pass/PassSystemInterface.h>
#include <Atom/RPI.Public/Scene.h>
#include <AzCore/Console/IConsole.h>
#include <AzCore/Math/Vector3.h>

namespace AZ::Render
{
    AZ_CVAR_EXTERNED(bool, r_portableGi);
    AZ_CVAR(
        float,
        r_portableGiIntensity,
        1.0f,
        nullptr,
        AZ::ConsoleFunctorFlags::Null,
        "Portable GI irradiance scale.");

    RPI::Ptr<DiffuseGlobalFullscreenPass> DiffuseGlobalFullscreenPass::Create(const RPI::PassDescriptor& descriptor)
    {
        return aznew DiffuseGlobalFullscreenPass(descriptor);
    }

    DiffuseGlobalFullscreenPass::DiffuseGlobalFullscreenPass(const RPI::PassDescriptor& descriptor)
        : RPI::FullscreenTrianglePass(descriptor)
    {
    }

    bool DiffuseGlobalFullscreenPass::IsEnabled() const
    {
        return static_cast<bool>(r_portableGi);
    }

    void DiffuseGlobalFullscreenPass::FrameBeginInternal(FramePrepareParams params)
    {
        m_boundProbeBuffer = nullptr;
        PortableGiUpdatePass* updatePass = nullptr;
        if (RPI::PassSystemInterface::Get() && GetScene())
        {
            const auto filter = RPI::PassFilter::CreateWithTemplateName(Name("PortableGiUpdateTemplate"), GetScene());
            RPI::PassSystemInterface::Get()->ForEachPass(
                filter,
                [&updatePass](RPI::Pass* pass)
                {
                    updatePass = azrtti_cast<PortableGiUpdatePass*>(pass);
                    return updatePass ? RPI::PassFilterExecutionFlow::StopVisitingPasses
                                      : RPI::PassFilterExecutionFlow::ContinueVisitingPasses;
                });
        }

        if (auto* srg = m_shaderResourceGroup.get())
        {
            srg->SetConstant(m_portableGiEnabledIndex, static_cast<bool>(r_portableGi) ? 1u : 0u);
            srg->SetConstant(m_portableGiIntensityIndex, static_cast<float>(r_portableGiIntensity));
            if (updatePass)
            {
                const auto& buffer = updatePass->GetCurrentProbeBuffer();
                if (buffer)
                {
                    srg->SetBufferView(m_probeBufferIndex, buffer->GetBufferView());
                    m_boundProbeBuffer = buffer;
                }
                const AZ::Vector3 origin = updatePass->GetGridOrigin();
                const AZStd::array<uint32_t, 3> dims = updatePass->GetGridDimensions();
                srg->SetConstant(m_gridOriginIndex, origin);
                srg->SetConstant(m_probeSpacingIndex, updatePass->GetProbeSpacing());
                srg->SetConstant(
                    m_gridDimsIndex,
                    AZ::Vector3(static_cast<float>(dims[0]), static_cast<float>(dims[1]), static_cast<float>(dims[2])));
            }
        }
        FullscreenTrianglePass::FrameBeginInternal(params);
    }

    void DiffuseGlobalFullscreenPass::SetupFrameGraphDependencies(RHI::FrameGraphInterface frameGraph)
    {
        FullscreenTrianglePass::SetupFrameGraphDependencies(frameGraph);

        if (!m_boundProbeBuffer)
        {
            return;
        }

        const RHI::AttachmentId& attachmentId = m_boundProbeBuffer->GetAttachmentId();
        auto attachmentDatabase = frameGraph.GetAttachmentDatabase();
        if (!attachmentDatabase.IsAttachmentValid(attachmentId))
        {
            [[maybe_unused]] const RHI::ResultCode result =
                attachmentDatabase.ImportBuffer(attachmentId, m_boundProbeBuffer->GetRHIBuffer());
            AZ_Assert(result == RHI::ResultCode::Success, "Failed to import Portable GI probe buffer with error %d", result);
        }

        RHI::BufferScopeAttachmentDescriptor descriptor;
        descriptor.m_attachmentId = attachmentId;
        descriptor.m_bufferViewDescriptor = m_boundProbeBuffer->GetBufferViewDescriptor();
        descriptor.m_loadStoreAction.m_loadAction = RHI::AttachmentLoadAction::Load;

        [[maybe_unused]] const RHI::ResultCode result =
            frameGraph.UseShaderAttachment(descriptor, RHI::ScopeAttachmentAccess::Read, RHI::ScopeAttachmentStage::FragmentShader);
        AZ_Assert(result == RHI::ResultCode::Success, "Failed to declare Portable GI probe buffer with error %d", result);
    }
} // namespace AZ::Render
