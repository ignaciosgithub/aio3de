/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <SoftBody/SoftBodyGpuSolverPass.h>
#include <SoftBody/SoftBodyGpuSolverSystem.h>

#include <AzCore/Interface/Interface.h>

#include <Atom/RPI.Public/Buffer/Buffer.h>
#include <Atom/RPI.Public/Pass/PassSystemInterface.h>

namespace AZ
{
    namespace Render
    {
        namespace
        {
            constexpr uint32_t ThreadsPerGroup = 256; // Must match NUM_THREADS in SoftBodyGpuSolve.azsl.

            SoftBodyGpuSolverSystem* GetSolverSystem()
            {
                // The interface is registered exclusively by CommonSystemComponent with the
                // concrete system, so the downcast is safe.
                return azrtti_cast<SoftBodyGpuSolverSystem*>(
                    AZ::Interface<SoftBodyGpuSolverInterface>::Get());
            }
        } // namespace

        RPI::Ptr<SoftBodyGpuSolverPass> SoftBodyGpuSolverPass::Create(const RPI::PassDescriptor& descriptor)
        {
            return aznew SoftBodyGpuSolverPass(descriptor);
        }

        SoftBodyGpuSolverPass::SoftBodyGpuSolverPass(const RPI::PassDescriptor& descriptor)
            : RPI::ComputePass(descriptor)
        {
        }

        void SoftBodyGpuSolverPass::BuildInternal()
        {
            SoftBodyGpuSolverSystem* system = GetSolverSystem();
            if (system)
            {
                system->PackBuffers();
                m_builtGeneration = system->GetGeneration();

                AttachBufferToSlot(Name("Particles"), system->GetPositionsBuffer());
                AttachBufferToSlot(Name("PrevPositions"), system->GetPrevPositionsBuffer());
                AttachBufferToSlot(Name("Velocities"), system->GetVelocitiesBuffer());
                AttachBufferToSlot(Name("ConstraintCorrections"), system->GetConstraintCorrectionsBuffer());
                AttachBufferToSlot(Name("ParticleGradients"), system->GetParticleGradientsBuffer());
                AttachBufferToSlot(Name("TriangleGradients"), system->GetTriangleGradientsBuffer());
            }

            ComputePass::BuildInternal();
        }

        void SoftBodyGpuSolverPass::FrameBeginInternal(FramePrepareParams params)
        {
            SoftBodyGpuSolverSystem* system = GetSolverSystem();

            uint32_t bodyCount = 0;
            if (system)
            {
                if (system->GetGeneration() != m_builtGeneration)
                {
                    // The body set changed: rebuild so BuildInternal re-packs and re-attaches
                    // the buffers. Skip dispatching against the stale layout this frame.
                    QueueForBuildAndInitialization();
                }
                else
                {
                    bodyCount = system->UpdateFrameParams();
                }
            }

            if (auto* srg = m_shaderResourceGroup.get())
            {
                srg->SetConstant(m_bodyCountIndex, bodyCount);
                if (system && bodyCount > 0)
                {
                    srg->SetBufferView(m_bodyParamsIndex, system->GetBodyParamsBuffer()->GetBufferView());
                    srg->SetBufferView(m_constraintParticlesIndex, system->GetConstraintParticlesBuffer()->GetBufferView());
                    srg->SetBufferView(m_constraintParamsIndex, system->GetConstraintParamsBuffer()->GetBufferView());
                    srg->SetBufferView(m_adjacencyIndex, system->GetAdjacencyBuffer()->GetBufferView());
                    srg->SetBufferView(m_adjacencyOffsetsIndex, system->GetAdjacencyOffsetsBuffer()->GetBufferView());
                    srg->SetBufferView(m_trianglesIndex, system->GetTrianglesBuffer()->GetBufferView());
                    srg->SetBufferView(m_triAdjacencyIndex, system->GetTriAdjacencyBuffer()->GetBufferView());
                    srg->SetBufferView(m_triAdjacencyOffsetsIndex, system->GetTriAdjacencyOffsetsBuffer()->GetBufferView());
                }
            }

            // One thread group per body.
            SetTargetThreadCounts(AZStd::max<uint32_t>(1, bodyCount) * ThreadsPerGroup, 1, 1);

            // Read the solved positions back asynchronously (results land a few frames later).
            if (system && bodyCount > 0)
            {
                if (AZStd::shared_ptr<RPI::AttachmentReadback> readback = system->AcquireReadback())
                {
                    ReadbackAttachment(readback, 0, Name("Particles"), RPI::PassAttachmentReadbackOption::Output);
                }
            }

            ComputePass::FrameBeginInternal(params);
        }
    } // namespace Render
} // namespace AZ
