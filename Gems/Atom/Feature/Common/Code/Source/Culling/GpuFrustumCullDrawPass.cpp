/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <Culling/GpuFrustumCullDrawPass.h>

#include <AzCore/Console/IConsole.h>
#include <AzCore/Math/Matrix4x4.h>
#include <AzCore/Math/Vector4.h>
#include <AzCore/std/containers/array.h>
#include <AzCore/std/containers/vector.h>

#include <Atom/RHI/CommandList.h>
#include <Atom/RHI/Factory.h>
#include <Atom/RHI/FrameGraphInterface.h>
#include <Atom/RHI/FrameGraphCompileContext.h>
#include <Atom/RHI/FrameGraphExecuteContext.h>
#include <Atom/RHI.Reflect/IndirectBufferLayout.h>
#include <Atom/RHI.Reflect/InputStreamLayoutBuilder.h>

#include <Atom/RPI.Public/Buffer/Buffer.h>
#include <Atom/RPI.Public/Buffer/BufferSystemInterface.h>
#include <Atom/RPI.Public/RenderPipeline.h>
#include <Atom/RPI.Public/Shader/Shader.h>
#include <Atom/RPI.Public/View.h>

namespace AZ
{
    namespace Render
    {
        // Opt-in gate: when false (default) the pass is a no-op even if wired into a pipeline.
        AZ_CVAR(
            bool,
            r_useGpuDrivenCulling,
            false,
            nullptr,
            AZ::ConsoleFunctorFlags::Null,
            "Enable the GPU-driven culling indirect draw (GpuFrustumCullDrawPass). Off by default; "
            "wire GpuFrustumCullPass + GpuFrustumCullDrawPass into a pipeline and profile on real hardware.");

        RPI::Ptr<GpuFrustumCullDrawPass> GpuFrustumCullDrawPass::Create(const RPI::PassDescriptor& descriptor)
        {
            return aznew GpuFrustumCullDrawPass(descriptor);
        }

        GpuFrustumCullDrawPass::GpuFrustumCullDrawPass(const RPI::PassDescriptor& descriptor)
            : RPI::RenderPass(descriptor)
        {
        }

        void GpuFrustumCullDrawPass::InitializeResources()
        {
            // Load the draw shader and create the pipeline state with a single float3 POSITION stream.
            m_shader = RPI::LoadCriticalShader(DrawShaderFilePath);
            if (!m_shader)
            {
                AZ_Error("GpuFrustumCullDrawPass", false, "Failed to load draw shader '%s'.", DrawShaderFilePath);
                return;
            }

            m_pipelineState = aznew RPI::PipelineStateForDraw;
            m_pipelineState->Init(m_shader);

            RHI::InputStreamLayoutBuilder layoutBuilder;
            layoutBuilder.AddBuffer()->Channel("POSITION", RHI::Format::R32G32B32_FLOAT);
            m_pipelineState->InputStreamLayout() = layoutBuilder.End();

            // Per-pass SRG.
            {
                auto perPassSrgLayout = m_shader->FindShaderResourceGroupLayout(RPI::SrgBindingSlot::Pass);
                if (!perPassSrgLayout)
                {
                    AZ_Error("GpuFrustumCullDrawPass", false, "Failed to find per-pass SRG layout.");
                    return;
                }
                m_resourceGroup = RPI::ShaderResourceGroup::Create(
                    m_shader->GetAsset(), m_shader->GetSupervariantIndex(), perPassSrgLayout->GetName());
                if (!m_resourceGroup)
                {
                    AZ_Error("GpuFrustumCullDrawPass", false, "Failed to create per-pass SRG.");
                    return;
                }
            }

            // Unit cube centered at the origin with half-extent 1 (scaled per instance).
            const AZStd::array<float, 8 * 3> cubeVertices = {
                -1.0f, -1.0f, -1.0f,
                 1.0f, -1.0f, -1.0f,
                 1.0f,  1.0f, -1.0f,
                -1.0f,  1.0f, -1.0f,
                -1.0f, -1.0f,  1.0f,
                 1.0f, -1.0f,  1.0f,
                 1.0f,  1.0f,  1.0f,
                -1.0f,  1.0f,  1.0f,
            };
            const AZStd::array<uint16_t, CubeIndexCount> cubeIndices = {
                0, 1, 2, 0, 2, 3, // -Z
                4, 6, 5, 4, 7, 6, // +Z
                0, 4, 5, 0, 5, 1, // -Y
                3, 2, 6, 3, 6, 7, // +Y
                0, 3, 7, 0, 7, 4, // -X
                1, 5, 6, 1, 6, 2, // +X
            };

            {
                RPI::CommonBufferDescriptor desc;
                desc.m_poolType = RPI::CommonBufferPoolType::StaticInputAssembly;
                desc.m_bufferName = "GpuFrustumCullDraw.CubeVertices";
                desc.m_elementSize = sizeof(float) * 3;
                desc.m_byteCount = cubeVertices.size() * sizeof(float);
                desc.m_bufferData = cubeVertices.data();
                m_cubeVertexBuffer = RPI::BufferSystemInterface::Get()->CreateBufferFromCommonPool(desc);
                m_vertexBufferView = RHI::StreamBufferView(
                    *m_cubeVertexBuffer->GetRHIBuffer(),
                    0,
                    aznumeric_cast<uint32_t>(desc.m_byteCount),
                    aznumeric_cast<uint32_t>(desc.m_elementSize));
            }

            {
                RPI::CommonBufferDescriptor desc;
                desc.m_poolType = RPI::CommonBufferPoolType::StaticInputAssembly;
                desc.m_bufferName = "GpuFrustumCullDraw.CubeIndices";
                desc.m_elementSize = sizeof(uint16_t);
                desc.m_byteCount = cubeIndices.size() * sizeof(uint16_t);
                desc.m_bufferData = cubeIndices.data();
                m_cubeIndexBuffer = RPI::BufferSystemInterface::Get()->CreateBufferFromCommonPool(desc);
                m_indexBufferView = RHI::IndexBufferView(
                    *m_cubeIndexBuffer->GetRHIBuffer(),
                    0,
                    aznumeric_cast<uint32_t>(desc.m_byteCount),
                    RHI::IndexFormat::Uint16);
            }

            // Per-instance placement (xyz = world center, w = uniform scale) and color, generated as a
            // grid. The cull pass should be fed a matching sphere set (same centers/radii) so the
            // compacted survivor indices index this data in range.
            {
                AZStd::vector<AZ::Vector4> placement(InstanceCount);
                AZStd::vector<AZ::Vector4> colors(InstanceCount);

                const uint32_t gridDim = static_cast<uint32_t>(AZStd::ceil(AZStd::sqrt(static_cast<float>(InstanceCount))));
                const float spacing = 4.0f;
                const float scale = 0.75f;
                const float halfExtent = 0.5f * spacing * static_cast<float>(gridDim - 1);

                for (uint32_t i = 0; i < InstanceCount; ++i)
                {
                    const uint32_t gx = i % gridDim;
                    const uint32_t gy = i / gridDim;
                    const float x = static_cast<float>(gx) * spacing - halfExtent;
                    const float y = static_cast<float>(gy) * spacing - halfExtent;
                    placement[i] = AZ::Vector4(x, y, 0.0f, scale);

                    const float r = static_cast<float>(gx) / static_cast<float>(gridDim);
                    const float g = static_cast<float>(gy) / static_cast<float>(gridDim);
                    colors[i] = AZ::Vector4(r, g, 1.0f - 0.5f * (r + g), 1.0f);
                }

                {
                    RPI::CommonBufferDescriptor desc;
                    desc.m_poolType = RPI::CommonBufferPoolType::ReadOnly;
                    desc.m_bufferName = "GpuFrustumCullDraw.InstanceData";
                    desc.m_elementSize = sizeof(AZ::Vector4);
                    desc.m_byteCount = static_cast<uint64_t>(desc.m_elementSize) * InstanceCount;
                    desc.m_bufferData = placement.data();
                    m_instanceDataBuffer = RPI::BufferSystemInterface::Get()->CreateBufferFromCommonPool(desc);
                }
                {
                    RPI::CommonBufferDescriptor desc;
                    desc.m_poolType = RPI::CommonBufferPoolType::ReadOnly;
                    desc.m_bufferName = "GpuFrustumCullDraw.InstanceColors";
                    desc.m_elementSize = sizeof(AZ::Vector4);
                    desc.m_byteCount = static_cast<uint64_t>(desc.m_elementSize) * InstanceCount;
                    desc.m_bufferData = colors.data();
                    m_instanceColorsBuffer = RPI::BufferSystemInterface::Get()->CreateBufferFromCommonPool(desc);
                }
            }

            // Indirect-buffer signature describing the single DrawIndexedIndirect command produced by
            // GpuFrustumCullPass.
            {
                RHI::IndirectBufferLayout layout;
                layout.AddIndirectCommand(RHI::IndirectCommandDescriptor(RHI::IndirectCommandType::DrawIndexed));
                if (!layout.Finalize())
                {
                    AZ_Error("GpuFrustumCullDrawPass", false, "Failed to finalize indirect buffer layout.");
                    return;
                }

                m_drawIndirectSignature = aznew RHI::IndirectBufferSignature;
                RHI::IndirectBufferSignatureDescriptor signatureDescriptor{};
                signatureDescriptor.m_layout = layout;
                [[maybe_unused]] const RHI::ResultCode result =
                    m_drawIndirectSignature->Init(RHI::MultiDevice::AllDevices, signatureDescriptor);
                AZ_Error("GpuFrustumCullDrawPass", result == RHI::ResultCode::Success, "Failed to init indirect signature.");
            }

            m_geometryView.SetIndexBufferView(m_indexBufferView);
            m_geometryView.AddStreamBufferView(m_vertexBufferView);
        }

        void GpuFrustumCullDrawPass::InitializeInternal()
        {
            Base::InitializeInternal();

            if (!m_resourcesInitialized)
            {
                InitializeResources();
                m_resourcesInitialized = true;
            }

            if (m_pipelineState)
            {
                m_pipelineState->SetOutputFromPass(this);
                m_pipelineState->Finalize();
            }
        }

        void GpuFrustumCullDrawPass::FrameBeginInternal(FramePrepareParams params)
        {
            // Resolve the cull-pass output bindings (available once attachments are built).
            m_visibleIndicesBinding = FindAttachmentBinding(Name(VisibleInstanceIndicesSlotName));
            m_indirectArgsBinding = FindAttachmentBinding(Name(DrawIndirectArgsSlotName));

            if (m_resourceGroup)
            {
                if (const RPI::ViewPtr view = GetView())
                {
                    m_resourceGroup->SetConstant(m_worldToClipIndex, view->GetWorldToClipMatrix());
                }
            }

            m_viewportState = params.m_viewportState;
            m_scissorState = RHI::Scissor(
                static_cast<int32_t>(params.m_viewportState.m_minX),
                static_cast<int32_t>(params.m_viewportState.m_minY),
                static_cast<int32_t>(params.m_viewportState.m_maxX),
                static_cast<int32_t>(params.m_viewportState.m_maxY));

            Base::FrameBeginInternal(params);
        }

        void GpuFrustumCullDrawPass::SetupFrameGraphDependencies(RHI::FrameGraphInterface frameGraph)
        {
            Base::SetupFrameGraphDependencies(frameGraph);
            frameGraph.SetEstimatedItemCount(1);
        }

        void GpuFrustumCullDrawPass::CompileResources(const RHI::FrameGraphCompileContext& context)
        {
            m_drawValid = false;

            if (!r_useGpuDrivenCulling || !m_resourceGroup || !m_drawIndirectSignature)
            {
                return;
            }

            // Bind the per-instance data owned by this pass.
            m_resourceGroup->SetBufferView(m_instanceDataIndex, m_instanceDataBuffer->GetBufferView());
            m_resourceGroup->SetBufferView(m_instanceColorsIndex, m_instanceColorsBuffer->GetBufferView());

            // Bind the compacted visible-index list produced by the cull pass.
            if (m_visibleIndicesBinding && m_visibleIndicesBinding->GetAttachment())
            {
                const auto attachmentId = m_visibleIndicesBinding->GetAttachment()->GetAttachmentId();
                if (const RHI::BufferView* visibleIndicesView = context.GetBufferView(attachmentId))
                {
                    m_resourceGroup->SetBufferView(m_visibleInstanceIndicesIndex, visibleIndicesView);
                }
            }

            m_resourceGroup->Compile();

            // Build the indirect draw arguments from the cull pass's DrawIndexedIndirect buffer.
            if (m_indirectArgsBinding && m_indirectArgsBinding->GetAttachment())
            {
                const auto attachmentId = m_indirectArgsBinding->GetAttachment()->GetAttachmentId();
                if (const RHI::Buffer* indirectBuffer = context.GetBuffer(attachmentId))
                {
                    const uint32_t commandStride = m_drawIndirectSignature->GetByteStride();
                    m_indirectBufferView = RHI::IndirectBufferView(
                        *indirectBuffer,
                        *m_drawIndirectSignature,
                        0,
                        commandStride,
                        commandStride);

                    m_geometryView.SetDrawArguments(RHI::DrawArguments(RHI::DrawIndirect(1, m_indirectBufferView, 0)));
                    m_drawValid = true;
                }
            }
        }

        void GpuFrustumCullDrawPass::BuildCommandListInternal(const RHI::FrameGraphExecuteContext& context)
        {
            if (!m_drawValid || !m_pipelineState || !m_resourceGroup)
            {
                return;
            }

            context.GetCommandList()->SetViewport(m_viewportState);
            context.GetCommandList()->SetScissor(m_scissorState);
            context.GetCommandList()->SetShaderResourceGroupForDraw(
                *m_resourceGroup->GetRHIShaderResourceGroup()->GetDeviceShaderResourceGroup(context.GetDeviceIndex()));

            RHI::DeviceDrawItem drawItem;
            drawItem.m_geometryView = m_geometryView.GetDeviceGeometryView(context.GetDeviceIndex());
            drawItem.m_streamIndices = m_geometryView.GetFullStreamBufferIndices();
            drawItem.m_pipelineState =
                m_pipelineState->GetRHIPipelineState()->GetDevicePipelineState(context.GetDeviceIndex()).get();

            context.GetCommandList()->Submit(drawItem);
        }
    } // namespace Render
} // namespace AZ
