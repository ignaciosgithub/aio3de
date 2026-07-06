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
#include <Atom/RPI.Public/Pass/ComputePass.h>

namespace AZ
{
    namespace Render
    {
        //! Compute pass running the GPU XPBD soft body solver (one thread group per body).
        //! Buffers are owned by SoftBodyGpuSolverSystem so simulation state survives
        //! pipeline rebuilds; the pass re-attaches them whenever the body set changes.
        class SoftBodyGpuSolverPass final
            : public RPI::ComputePass
        {
            AZ_RPI_PASS(SoftBodyGpuSolverPass);

        public:
            AZ_RTTI(SoftBodyGpuSolverPass, "{9C4E5D2A-7B31-4F86-92E0-1A2B3C4D5E6F}", RPI::ComputePass);
            AZ_CLASS_ALLOCATOR(SoftBodyGpuSolverPass, SystemAllocator);

            static RPI::Ptr<SoftBodyGpuSolverPass> Create(const RPI::PassDescriptor& descriptor);

        protected:
            explicit SoftBodyGpuSolverPass(const RPI::PassDescriptor& descriptor);

            // RPI::Pass overrides...
            void BuildInternal() override;
            void FrameBeginInternal(FramePrepareParams params) override;

        private:
            uint32_t m_builtGeneration = 0;

            RHI::ShaderInputNameIndex m_bodyParamsIndex = "m_bodyParams";
            RHI::ShaderInputNameIndex m_constraintParticlesIndex = "m_constraintParticles";
            RHI::ShaderInputNameIndex m_constraintParamsIndex = "m_constraintParams";
            RHI::ShaderInputNameIndex m_adjacencyIndex = "m_adjacency";
            RHI::ShaderInputNameIndex m_adjacencyOffsetsIndex = "m_adjacencyOffsets";
            RHI::ShaderInputNameIndex m_trianglesIndex = "m_triangles";
            RHI::ShaderInputNameIndex m_triAdjacencyIndex = "m_triAdjacency";
            RHI::ShaderInputNameIndex m_triAdjacencyOffsetsIndex = "m_triAdjacencyOffsets";
            RHI::ShaderInputNameIndex m_bodyCountIndex = "m_bodyCount";
        };
    } // namespace Render
} // namespace AZ
