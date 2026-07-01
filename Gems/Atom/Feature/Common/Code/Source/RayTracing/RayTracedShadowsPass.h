/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzCore/Math/RayTracedShadows.h>
#include <AzCore/Math/RayTracingBvh.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/Math/Vector4.h>
#include <AzCore/Memory/SystemAllocator.h>
#include <AzCore/std/containers/vector.h>

#include <Atom/RHI.Reflect/ShaderInputNameIndex.h>

#include <Atom/RPI.Public/Buffer/Buffer.h>
#include <Atom/RPI.Public/Pass/ComputePass.h>

namespace AZ
{
    namespace Render
    {
        //! Hardware-agnostic, compute-shader hard ray-traced shadow pass.
        //!
        //! Builds a portable BVH (AZ::RayTracingBvh) over the occluder triangles on the CPU, uploads
        //! it plus a batch of shaded surface samples (world position + normal), and dispatches the
        //! RayTracedShadows compute shader: one thread per sample casts a single occlusion ray toward
        //! the light and writes visibility (1 = lit, 0 = shadowed). The any-hit traversal is the GPU
        //! mirror of AZ::ComputeShadowVisibility; it uses only standard compute + structured buffers,
        //! with NO dependency on DXR / VK_KHR_ray_tracing or RT cores, so it runs on every RHI backend
        //! (Vulkan, DX12, Metal, Null) and any GPU. This is the first concrete consumer of the
        //! hardware-agnostic BVH core (see RayTracingBvhPass).
        //!
        //! Opt-in: registered with the pass system but not part of any default render pipeline, so
        //! existing pipelines are unaffected until it is explicitly added to one.
        class RayTracedShadowsPass final
            : public RPI::ComputePass
        {
            AZ_RPI_PASS(RayTracedShadowsPass);

        public:
            AZ_RTTI(RayTracedShadowsPass, "{7D2A9C14-5E63-4B80-9C1F-2A3B4C5D6E70}", RPI::ComputePass);
            AZ_CLASS_ALLOCATOR(RayTracedShadowsPass, SystemAllocator);
            ~RayTracedShadowsPass() = default;

            static RPI::Ptr<RayTracedShadowsPass> Create(const RPI::PassDescriptor& descriptor);

            //! One shaded surface sample. Layout matches the shader's ShadowSampleGpu (2 x float4 =
            //! 32 bytes); only xyz of each is used.
            struct SampleGpu
            {
                AZStd::array<float, 4> m_position = { { 0.0f, 0.0f, 0.0f, 0.0f } };
                AZStd::array<float, 4> m_normal = { { 0.0f, 0.0f, 1.0f, 0.0f } };
            };

            //! Builds the BVH over the occluder \p triangles and marks the GPU geometry buffers dirty.
            void SetOccluderGeometry(const AZStd::vector<AZ::BvhTriangle>& triangles);

            //! Sets the surface samples to shade this dispatch and marks the sample/visibility buffers dirty.
            void SetSamples(const AZStd::vector<AZ::ShadowSample>& samples);

            //! Sets the shared shadow-ray parameters (light direction, max distance, normal bias).
            void SetShadowParams(const AZ::ShadowRayParams& params);

            //! Output buffer of visibility, one float per sample (1 = lit, 0 = shadowed).
            const Data::Instance<RPI::Buffer>& GetVisibilityBuffer() const { return m_visibilityBuffer; }

        protected:
            RayTracedShadowsPass(const RPI::PassDescriptor& descriptor);

            // Pass behavior overrides...
            void BuildInternal() override;
            void FrameBeginInternal(FramePrepareParams params) override;

        private:
            void CreateBuffers();

            // Slot names matching the pass template and the shader's PassSrg buffer inputs.
            static constexpr const char* NodesSlotName = "Nodes";
            static constexpr const char* TriangleVerticesSlotName = "TriangleVertices";
            static constexpr const char* SamplesSlotName = "Samples";
            static constexpr const char* VisibilitySlotName = "Visibility";

            AZ::RayTracingBvh m_bvh;

            // Packed, GPU-ready geometry derived from m_bvh in SetOccluderGeometry.
            AZStd::vector<AZ::Vector4> m_triangleVertices; // 3 per ordered triangle, xyz used.
            AZStd::vector<SampleGpu> m_samples;
            AZ::ShadowRayParams m_params;

            Data::Instance<RPI::Buffer> m_nodesBuffer;
            Data::Instance<RPI::Buffer> m_triangleVerticesBuffer;
            Data::Instance<RPI::Buffer> m_samplesBuffer;
            Data::Instance<RPI::Buffer> m_visibilityBuffer;

            bool m_buffersDirty = false;

            // SRG constant binding indices.
            AZ::RHI::ShaderInputNameIndex m_toLightIndex = "m_toLight";
            AZ::RHI::ShaderInputNameIndex m_maxDistanceIndex = "m_maxDistance";
            AZ::RHI::ShaderInputNameIndex m_normalBiasIndex = "m_normalBias";
            AZ::RHI::ShaderInputNameIndex m_sampleCountIndex = "m_sampleCount";
            AZ::RHI::ShaderInputNameIndex m_nodeCountIndex = "m_nodeCount";
        };
    } // namespace Render
} // namespace AZ
