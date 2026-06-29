/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzCore/Math/RayTracingBvh.h>
#include <AzCore/Memory/SystemAllocator.h>
#include <AzCore/std/containers/array.h>
#include <AzCore/std/containers/vector.h>

#include <Atom/RHI.Reflect/ShaderInputNameIndex.h>

#include <Atom/RPI.Public/Buffer/Buffer.h>
#include <Atom/RPI.Public/Pass/ComputePass.h>

namespace AZ
{
    namespace Render
    {
        //! Hardware-agnostic, compute-shader ray-tracing pass.
        //!
        //! Builds a portable BVH (AZ::RayTracingBvh) over a triangle soup on the CPU, uploads the
        //! nodes / triangles / primitive indices to structured buffers, and dispatches the
        //! RayTracingBvhTraverse compute shader: one thread per ray walks the BVH with a fixed
        //! traversal stack and writes its closest triangle hit. It uses only standard compute +
        //! structured buffers, with NO dependency on DXR / VK_KHR_ray_tracing or RT cores, so it
        //! runs on every RHI backend (Vulkan, DX12, Metal, Null) and any GPU.
        //!
        //! This is an opt-in building block: registered with the pass system but not part of any
        //! default render pipeline, so existing pipelines are unaffected. Wire it into a pipeline
        //! and feed it geometry + rays to drive a compute-RT effect (reflections / AO / shadows),
        //! then profile on target hardware. The CPU AZ::RayTracingBvh is the verified reference the
        //! shader mirrors line-for-line.
        class RayTracingBvhPass final
            : public RPI::ComputePass
        {
            AZ_RPI_PASS(RayTracingBvhPass);

        public:
            AZ_RTTI(RayTracingBvhPass, "{2B7E4D90-1A3C-4E62-9D5F-6A0B1C2D3E40}", RPI::ComputePass);
            AZ_CLASS_ALLOCATOR(RayTracingBvhPass, SystemAllocator);
            ~RayTracingBvhPass() = default;

            static RPI::Ptr<RayTracingBvhPass> Create(const RPI::PassDescriptor& descriptor);

            //! A single ray to trace. Layout matches the shader's BvhRay (32 bytes): a hit is
            //! accepted for 0 < t <= m_tMax along origin + t * direction.
            struct Ray
            {
                AZStd::array<float, 3> m_origin = { { 0.0f, 0.0f, 0.0f } };
                float m_tMax = 0.0f;
                AZStd::array<float, 3> m_direction = { { 0.0f, 0.0f, 1.0f } };
                uint32_t m_padding = 0;
            };

            //! Builds the BVH over \p triangles and marks the GPU geometry buffers dirty.
            void SetGeometry(const AZStd::vector<AZ::BvhTriangle>& triangles);

            //! Sets the rays to trace this dispatch and marks the ray/hit buffers dirty.
            void SetRays(const AZStd::vector<Ray>& rays);

            //! Output buffer of hits, one per ray (shader BvhRayHit: float t,u,v; uint primitiveId, hit).
            const Data::Instance<RPI::Buffer>& GetHitsBuffer() const { return m_hitsBuffer; }

        protected:
            RayTracingBvhPass(const RPI::PassDescriptor& descriptor);

            // Pass behavior overrides...
            void BuildInternal() override;
            void FrameBeginInternal(FramePrepareParams params) override;

        private:
            void CreateBuffers();

            // GPU layout of one BvhRayHit (float t,u,v; uint primitiveId; uint hit) = 20 bytes.
            static constexpr uint32_t HitElementSize = 5 * sizeof(uint32_t);

            // Slot names matching the pass template and the shader's PassSrg buffer inputs.
            static constexpr const char* NodesSlotName = "Nodes";
            static constexpr const char* TriangleVerticesSlotName = "TriangleVertices";
            static constexpr const char* PrimitiveIndicesSlotName = "PrimitiveIndices";
            static constexpr const char* RaysSlotName = "Rays";
            static constexpr const char* HitsSlotName = "Hits";

            AZ::RayTracingBvh m_bvh;

            // Packed, GPU-ready geometry derived from m_bvh in SetGeometry.
            AZStd::vector<AZ::Vector4> m_triangleVertices; // 3 per ordered triangle, xyz used.
            AZStd::vector<Ray> m_rays;

            Data::Instance<RPI::Buffer> m_nodesBuffer;
            Data::Instance<RPI::Buffer> m_triangleVerticesBuffer;
            Data::Instance<RPI::Buffer> m_primitiveIndicesBuffer;
            Data::Instance<RPI::Buffer> m_raysBuffer;
            Data::Instance<RPI::Buffer> m_hitsBuffer;

            bool m_buffersDirty = false;

            // SRG constant binding indices.
            AZ::RHI::ShaderInputNameIndex m_rayCountIndex = "m_rayCount";
            AZ::RHI::ShaderInputNameIndex m_nodeCountIndex = "m_nodeCount";
        };
    } // namespace Render
} // namespace AZ
