/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzCore/Memory/SystemAllocator.h>
#include <AzCore/std/containers/vector.h>

#include <Atom/RHI.Reflect/ShaderInputNameIndex.h>

#include <Atom/RPI.Public/Buffer/Buffer.h>
#include <Atom/RPI.Public/Pass/ComputePass.h>

namespace AZ
{
    namespace Render
    {
        //! GPU-driven broad-phase frustum culling pass.
        //!
        //! Runs the GpuFrustumCull compute shader: one thread tests one instance's bounding
        //! sphere against the view frustum and, if visible, compacts its index into an output
        //! buffer while bumping the instanceCount of a DrawIndexedIndirect arguments buffer.
        //! A subsequent indirect draw then renders exactly the surviving instances, so the CPU
        //! never touches per-instance visibility. This is the GPU counterpart of the SoA+SIMD
        //! CPU kernel AZ::FrustumClassifySpheres.
        //!
        //! This is an opt-in building block: it is registered with the pass system but is not
        //! part of any default render pipeline, so existing pipelines are unaffected. Wire it
        //! into a pipeline (and feed it real per-instance data via SetInstanceSpheres) to drive
        //! GPU culling, then profile on target hardware.
        class GpuFrustumCullPass final
            : public RPI::ComputePass
        {
            AZ_RPI_PASS(GpuFrustumCullPass);

        public:
            AZ_RTTI(GpuFrustumCullPass, "{0F1B5A6C-2D3E-4F50-9A7B-1C2D3E4F5061}", RPI::ComputePass);
            AZ_CLASS_ALLOCATOR(GpuFrustumCullPass, SystemAllocator);
            ~GpuFrustumCullPass() = default;

            static RPI::Ptr<GpuFrustumCullPass> Create(const RPI::PassDescriptor& descriptor);

            //! Sets the per-instance bounding spheres (xyz = world center, w = radius) to cull.
            //! @param indexCountPerInstance the index count written into the indirect draw args.
            void SetInstanceSpheres(const AZStd::vector<AZ::Vector4>& spheres, uint32_t indexCountPerInstance);

            //! Returns the indirect draw-arguments buffer (5 x uint, DrawIndexedIndirect layout)
            //! that the cull populates; element [1] is the surviving instance count.
            const Data::Instance<RPI::Buffer>& GetIndirectArgsBuffer() const { return m_drawIndirectArgsBuffer; }

            //! Returns the compacted list of visible instance indices.
            const Data::Instance<RPI::Buffer>& GetVisibleInstanceIndicesBuffer() const { return m_visibleInstanceIndicesBuffer; }

        protected:
            GpuFrustumCullPass(const RPI::PassDescriptor& descriptor);

            // Pass behavior overrides...
            void BuildInternal() override;
            void FrameBeginInternal(FramePrepareParams params) override;

        private:
            void CreateBuffers();

            // DrawIndexedIndirect args: indexCountPerInstance, instanceCount, startIndex, baseVertex, startInstance.
            static constexpr uint32_t DrawIndexedIndirectArgCount = 5;
            static constexpr uint32_t FrustumPlaneCount = 6;

            // Slot names matching the pass template and the shader's PassSrg buffer inputs.
            static constexpr const char* InstanceSpheresSlotName = "InstanceSpheres";
            static constexpr const char* VisibleInstanceIndicesSlotName = "VisibleInstanceIndices";
            static constexpr const char* DrawIndirectArgsSlotName = "DrawIndirectArgs";

            Data::Instance<RPI::Buffer> m_instanceSpheresBuffer;
            Data::Instance<RPI::Buffer> m_visibleInstanceIndicesBuffer;
            Data::Instance<RPI::Buffer> m_drawIndirectArgsBuffer;

            AZStd::vector<AZ::Vector4> m_instanceSpheres;
            uint32_t m_indexCountPerInstance = 0;
            bool m_buffersDirty = false;

            // SRG constant binding indices.
            AZ::RHI::ShaderInputNameIndex m_frustumPlanesIndex = "m_frustumPlanes";
            AZ::RHI::ShaderInputNameIndex m_instanceCountIndex = "m_instanceCount";
        };
    } // namespace Render
} // namespace AZ
