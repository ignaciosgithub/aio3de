/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include <DiffuseGlobalIllumination/DiffuseGlobalFullscreenPass.h>

#include <RayTracing/PortableGiUpdatePass.h>

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

    void DiffuseGlobalFullscreenPass::FrameBeginInternal(FramePrepareParams params)
    {
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
} // namespace AZ::Render
