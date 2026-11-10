/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <Atom/RPI.Public/Pass/ComputePass.h>
#include <Atom/RPI.Public/Pass/ParentPass.h>

namespace AZ::Render
{
    class FsrUpscaleParentPass final : public RPI::ParentPass
    {
        AZ_RPI_PASS(FsrUpscaleParentPass);

    public:
        AZ_RTTI(FsrUpscaleParentPass, "{F8E3C0A7-73A2-41C9-90A2-7F3065238BC2}", RPI::ParentPass);
        AZ_CLASS_ALLOCATOR(FsrUpscaleParentPass, SystemAllocator);

        static RPI::Ptr<FsrUpscaleParentPass> Create(const RPI::PassDescriptor& descriptor);
        bool IsEnabled() const override;

    private:
        explicit FsrUpscaleParentPass(const RPI::PassDescriptor& descriptor);
    };

    class FsrEasuPass final : public RPI::ComputePass
    {
        AZ_RPI_PASS(FsrEasuPass);

    public:
        AZ_RTTI(FsrEasuPass, "{641B77BB-A081-4C28-8AFE-36A00D3112B3}", RPI::ComputePass);
        AZ_CLASS_ALLOCATOR(FsrEasuPass, SystemAllocator);

        static RPI::Ptr<FsrEasuPass> Create(const RPI::PassDescriptor& descriptor);

    private:
        explicit FsrEasuPass(const RPI::PassDescriptor& descriptor);
        void FrameBeginInternal(FramePrepareParams params) override;

        RHI::ShaderInputNameIndex m_con0Index = "m_con0";
        RHI::ShaderInputNameIndex m_con1Index = "m_con1";
        RHI::ShaderInputNameIndex m_con2Index = "m_con2";
        RHI::ShaderInputNameIndex m_con3Index = "m_con3";
    };

    class FsrRcasPass final : public RPI::ComputePass
    {
        AZ_RPI_PASS(FsrRcasPass);

    public:
        AZ_RTTI(FsrRcasPass, "{B3858C12-6F6C-49DD-996D-B014FF3E9461}", RPI::ComputePass);
        AZ_CLASS_ALLOCATOR(FsrRcasPass, SystemAllocator);

        static RPI::Ptr<FsrRcasPass> Create(const RPI::PassDescriptor& descriptor);

    private:
        explicit FsrRcasPass(const RPI::PassDescriptor& descriptor);
        void FrameBeginInternal(FramePrepareParams params) override;

        RHI::ShaderInputNameIndex m_constantsIndex = "m_con";
    };
} // namespace AZ::Render
