/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <Atom/RHI.Reflect/ShaderInputNameIndex.h>
#include <Atom/RPI.Public/Pass/FullscreenTrianglePass.h>

namespace AZ::Render
{
    class DiffuseGlobalFullscreenPass final
        : public RPI::FullscreenTrianglePass
    {
        AZ_RPI_PASS(DiffuseGlobalFullscreenPass);

    public:
        AZ_RTTI(DiffuseGlobalFullscreenPass, "{452C23A4-7C13-4BC9-98F3-D98B71690A48}", RPI::FullscreenTrianglePass);
        AZ_CLASS_ALLOCATOR(DiffuseGlobalFullscreenPass, SystemAllocator);

        static RPI::Ptr<DiffuseGlobalFullscreenPass> Create(const RPI::PassDescriptor& descriptor);

    protected:
        DiffuseGlobalFullscreenPass(const RPI::PassDescriptor& descriptor);
        void FrameBeginInternal(FramePrepareParams params) override;

    private:
        AZ::RHI::ShaderInputNameIndex m_probeBufferIndex = "m_probeBuffer";
        AZ::RHI::ShaderInputNameIndex m_gridOriginIndex = "m_probeGridOrigin";
        AZ::RHI::ShaderInputNameIndex m_probeSpacingIndex = "m_probeSpacing";
        AZ::RHI::ShaderInputNameIndex m_gridDimsIndex = "m_probeGridDims";
        AZ::RHI::ShaderInputNameIndex m_portableGiEnabledIndex = "m_portableGiEnabled";
        AZ::RHI::ShaderInputNameIndex m_portableGiIntensityIndex = "m_portableGiIntensity";
    };
} // namespace AZ::Render
