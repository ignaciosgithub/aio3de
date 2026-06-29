/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzCore/Memory/SystemAllocator.h>

#include <Atom/RHI.Reflect/ShaderInputNameIndex.h>
#include <Atom/RHI/GeometryView.h>
#include <Atom/RHI/IndexBufferView.h>
#include <Atom/RHI/IndirectBufferSignature.h>
#include <Atom/RHI/StreamBufferView.h>

#include <Atom/RPI.Public/Buffer/Buffer.h>
#include <Atom/RPI.Public/Pass/RenderPass.h>
#include <Atom/RPI.Public/PipelineState.h>
#include <Atom/RPI.Public/Shader/Shader.h>
#include <Atom/RPI.Public/Shader/ShaderResourceGroup.h>

namespace AZ
{
    namespace Render
    {
        //! Consumer half of the GPU-driven culling pipeline.
        //!
        //! GpuFrustumCullPass classifies per-instance bounding spheres against the view frustum,
        //! compacts the surviving instance indices, and accumulates the survivor count into a
        //! DrawIndexedIndirect arguments buffer. This pass connects to those two outputs and issues
        //! a single DrawIndexedIndirect that renders exactly the surviving instances, so the CPU
        //! never walks the per-instance visibility list. The vertex shader reads the compacted
        //! visible-index list (SV_InstanceID -> original instance index) and looks up that
        //! instance's world placement and color to draw one unit cube per survivor.
        //!
        //! This is an opt-in building block: it is registered with the pass system but is not part
        //! of any default render pipeline. It is gated behind the r_useGpuDrivenCulling cvar so the
        //! draw is skipped unless explicitly enabled. Wire it after a GpuFrustumCullPass in a test
        //! pipeline and profile on target hardware (GPU frame-time cannot be measured headless).
        class GpuFrustumCullDrawPass final
            : public RPI::RenderPass
        {
            using Base = RPI::RenderPass;
            AZ_RPI_PASS(GpuFrustumCullDrawPass);

        public:
            AZ_RTTI(GpuFrustumCullDrawPass, "{8E2C1D4A-6B3F-4C57-9D81-2A3B4C5D6E7F}", RPI::RenderPass);
            AZ_CLASS_ALLOCATOR(GpuFrustumCullDrawPass, SystemAllocator);
            ~GpuFrustumCullDrawPass() = default;

            static RPI::Ptr<GpuFrustumCullDrawPass> Create(const RPI::PassDescriptor& descriptor);

        protected:
            explicit GpuFrustumCullDrawPass(const RPI::PassDescriptor& descriptor);

            // Pass behavior overrides...
            void InitializeInternal() override;
            void FrameBeginInternal(FramePrepareParams params) override;

            // Scope producer functions...
            void SetupFrameGraphDependencies(RHI::FrameGraphInterface frameGraph) override;
            void CompileResources(const RHI::FrameGraphCompileContext& context) override;
            void BuildCommandListInternal(const RHI::FrameGraphExecuteContext& context) override;

        private:
            // Builds the unit-cube geometry, the per-instance placement/color buffers and the
            // indirect-buffer signature. Called once on first initialization.
            void InitializeResources();

            // Number of instances the per-instance placement/color buffers are generated for. The
            // cull pass should be fed a matching sphere set so survivor indices stay in range.
            static constexpr uint32_t InstanceCount = 1024;
            static constexpr uint32_t CubeIndexCount = 36;

            // Slot names matching the pass template.
            static constexpr const char* VisibleInstanceIndicesSlotName = "VisibleInstanceIndices";
            static constexpr const char* DrawIndirectArgsSlotName = "DrawIndirectArgs";

            static constexpr const char* DrawShaderFilePath = "Shaders/Culling/GpuFrustumCullDraw.shader";

            RHI::Ptr<RPI::PipelineStateForDraw> m_pipelineState;
            Data::Instance<RPI::Shader> m_shader;
            Data::Instance<RPI::ShaderResourceGroup> m_resourceGroup;

            // SRG bindings.
            RHI::ShaderInputNameIndex m_worldToClipIndex = "m_worldToClip";
            RHI::ShaderInputNameIndex m_visibleInstanceIndicesIndex = "m_visibleInstanceIndices";
            RHI::ShaderInputNameIndex m_instanceDataIndex = "m_instanceData";
            RHI::ShaderInputNameIndex m_instanceColorsIndex = "m_instanceColors";

            // Unit-cube geometry shared by every drawn instance.
            Data::Instance<RPI::Buffer> m_cubeVertexBuffer;
            Data::Instance<RPI::Buffer> m_cubeIndexBuffer;
            RHI::StreamBufferView m_vertexBufferView;
            RHI::IndexBufferView m_indexBufferView;

            // Per-instance world placement (xyz = center, w = scale) and color, owned by this pass.
            Data::Instance<RPI::Buffer> m_instanceDataBuffer;
            Data::Instance<RPI::Buffer> m_instanceColorsBuffer;

            // Signature describing the single DrawIndexedIndirect command in the cull pass's args buffer.
            RHI::Ptr<RHI::IndirectBufferSignature> m_drawIndirectSignature;
            RHI::IndirectBufferView m_indirectBufferView;
            RHI::GeometryView m_geometryView{ RHI::MultiDevice::AllDevices };

            // Resolved attachment bindings for the cull pass outputs.
            RPI::PassAttachmentBinding* m_visibleIndicesBinding = nullptr;
            RPI::PassAttachmentBinding* m_indirectArgsBinding = nullptr;

            RHI::Viewport m_viewportState;
            RHI::Scissor m_scissorState;

            bool m_resourcesInitialized = false;
            bool m_drawValid = false;
        };
    } // namespace Render
} // namespace AZ
