/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <Culling/GpuFrustumCullPass.h>

#include <AzCore/Math/Frustum.h>
#include <AzCore/Math/Plane.h>
#include <AzCore/Math/Vector4.h>

#include <Atom/RPI.Public/Buffer/BufferSystemInterface.h>
#include <Atom/RPI.Public/Buffer/Buffer.h>
#include <Atom/RPI.Public/RenderPipeline.h>
#include <Atom/RPI.Public/View.h>

namespace AZ
{
    namespace Render
    {
        RPI::Ptr<GpuFrustumCullPass> GpuFrustumCullPass::Create(const RPI::PassDescriptor& descriptor)
        {
            return aznew GpuFrustumCullPass(descriptor);
        }

        GpuFrustumCullPass::GpuFrustumCullPass(const RPI::PassDescriptor& descriptor)
            : RPI::ComputePass(descriptor)
        {
        }

        void GpuFrustumCullPass::CreateBuffers()
        {
            const uint32_t instanceCount = AZStd::max<uint32_t>(1, static_cast<uint32_t>(m_instanceSpheres.size()));

            // Input: per-instance bounding spheres (float4).
            {
                RPI::CommonBufferDescriptor desc;
                desc.m_poolType = RPI::CommonBufferPoolType::ReadOnly;
                desc.m_bufferName = "GpuFrustumCull.InstanceSpheres";
                desc.m_elementSize = sizeof(AZ::Vector4);
                desc.m_byteCount = static_cast<uint64_t>(desc.m_elementSize) * instanceCount;
                desc.m_bufferData = m_instanceSpheres.empty() ? nullptr : m_instanceSpheres.data();
                m_instanceSpheresBuffer = RPI::BufferSystemInterface::Get()->CreateBufferFromCommonPool(desc);
            }

            // Output: compacted list of visible instance indices (uint).
            {
                RPI::CommonBufferDescriptor desc;
                desc.m_poolType = RPI::CommonBufferPoolType::ReadWrite;
                desc.m_bufferName = "GpuFrustumCull.VisibleInstanceIndices";
                desc.m_elementSize = sizeof(uint32_t);
                desc.m_byteCount = static_cast<uint64_t>(desc.m_elementSize) * instanceCount;
                desc.m_bufferData = nullptr;
                m_visibleInstanceIndicesBuffer = RPI::BufferSystemInterface::Get()->CreateBufferFromCommonPool(desc);
            }

            // Output: indirect DrawIndexedIndirect arguments (5 x uint).
            {
                const uint32_t initialArgs[DrawIndexedIndirectArgCount] = { m_indexCountPerInstance, 0, 0, 0, 0 };
                RPI::CommonBufferDescriptor desc;
                desc.m_poolType = RPI::CommonBufferPoolType::ReadWrite;
                desc.m_bufferName = "GpuFrustumCull.DrawIndirectArgs";
                desc.m_elementSize = sizeof(uint32_t);
                desc.m_byteCount = sizeof(initialArgs);
                desc.m_bufferData = initialArgs;
                m_drawIndirectArgsBuffer = RPI::BufferSystemInterface::Get()->CreateBufferFromCommonPool(desc);
            }

            m_buffersDirty = false;
        }

        void GpuFrustumCullPass::SetInstanceSpheres(const AZStd::vector<AZ::Vector4>& spheres, uint32_t indexCountPerInstance)
        {
            m_instanceSpheres = spheres;
            m_indexCountPerInstance = indexCountPerInstance;
            m_buffersDirty = true;
        }

        void GpuFrustumCullPass::BuildInternal()
        {
            if (!m_drawIndirectArgsBuffer || m_buffersDirty)
            {
                CreateBuffers();
            }

            AttachBufferToSlot(InstanceSpheresSlotName, m_instanceSpheresBuffer);
            AttachBufferToSlot(VisibleInstanceIndicesSlotName, m_visibleInstanceIndicesBuffer);
            AttachBufferToSlot(DrawIndirectArgsSlotName, m_drawIndirectArgsBuffer);

            ComputePass::BuildInternal();
        }

        void GpuFrustumCullPass::FrameBeginInternal(FramePrepareParams params)
        {
            const uint32_t instanceCount = static_cast<uint32_t>(m_instanceSpheres.size());

            // Reset the indirect-args instanceCount to 0 each frame; the compute shader accumulates it.
            if (m_drawIndirectArgsBuffer)
            {
                const uint32_t resetArgs[DrawIndexedIndirectArgCount] = { m_indexCountPerInstance, 0, 0, 0, 0 };
                m_drawIndirectArgsBuffer->UpdateData(resetArgs, sizeof(resetArgs));
            }

            // Derive the six frustum planes from the view's world-to-clip matrix and pack them as
            // float4(nx, ny, nz, d), matching AZ::Plane / AZ::FrustumClassifySpheres.
            if (auto* srg = m_shaderResourceGroup.get())
            {
                const RPI::ViewPtr view = GetView();
                if (view)
                {
                    const AZ::Frustum frustum =
                        AZ::Frustum::CreateFromMatrixColumnMajor(view->GetWorldToClipMatrix());

                    AZStd::array<AZ::Vector4, FrustumPlaneCount> planes;
                    for (uint32_t planeId = 0; planeId < FrustumPlaneCount; ++planeId)
                    {
                        const AZ::Plane plane = frustum.GetPlane(static_cast<AZ::Frustum::PlaneId>(planeId));
                        planes[planeId] = plane.GetPlaneEquationCoefficients();
                    }
                    srg->SetConstantArray(m_frustumPlanesIndex, planes);
                }

                srg->SetConstant(m_instanceCountIndex, instanceCount);
            }

            // One thread per instance.
            SetTargetThreadCounts(AZStd::max<uint32_t>(1, instanceCount), 1, 1);

            ComputePass::FrameBeginInternal(params);
        }
    } // namespace Render
} // namespace AZ
