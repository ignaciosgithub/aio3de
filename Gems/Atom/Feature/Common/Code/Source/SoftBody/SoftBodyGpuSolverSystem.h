/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <Atom/Feature/SoftBody/SoftBodyGpuSolverInterface.h>

#include <AzCore/Memory/SystemAllocator.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/parallel/mutex.h>
#include <AzCore/std/smart_ptr/shared_ptr.h>

#include <Atom/RPI.Public/Buffer/Buffer.h>
#include <Atom/RPI.Public/Pass/AttachmentReadback.h>

namespace AZ
{
    namespace Render
    {
        //! Per-body simulation parameters uploaded each frame. Must match the
        //! SoftBodyGpuBodyParams struct in SoftBodyGpuSolve.azsl field for field.
        struct SoftBodyGpuBodyParams
        {
            uint32_t m_particleOffset = 0;
            uint32_t m_particleCount = 0;
            uint32_t m_constraintOffset = 0;
            uint32_t m_constraintCount = 0;
            uint32_t m_triangleOffset = 0;
            uint32_t m_triangleCount = 0;
            uint32_t m_substeps = 1;
            uint32_t m_iterations = 1;
            float m_gravity[3] = { 0.0f, 0.0f, -9.81f };
            float m_dtPerSubstep = 0.0f;
            float m_damping = 0.0f;
            float m_groundHeight = 0.0f;
            uint32_t m_groundEnabled = 0;
            float m_groundFriction = 0.0f;
            float m_pressure = 0.0f;
            float m_pressureCompliance = 0.0f;
            float m_restVolume = 0.0f;
            uint32_t m_stepQueued = 0;
        };

        //! CPU-side registry and GPU buffer manager behind SoftBodyGpuSolverInterface.
        //! Owned by CommonSystemComponent (so bodies survive render pipeline rebuilds);
        //! SoftBodyGpuSolverPass pulls the packed buffers from here each frame.
        class SoftBodyGpuSolverSystem final
            : public SoftBodyGpuSolverInterface
        {
        public:
            AZ_RTTI(SoftBodyGpuSolverSystem, "{7D3F2A81-5C6B-4E90-A1D2-3E4F5A6B7C8D}", SoftBodyGpuSolverInterface);
            AZ_CLASS_ALLOCATOR(SoftBodyGpuSolverSystem, SystemAllocator);

            void Activate();
            void Deactivate();

            // SoftBodyGpuSolverInterface overrides...
            SoftBodyGpuBodyHandle RegisterBody(const SoftBodyGpuBodyDesc& desc) override;
            void UnregisterBody(SoftBodyGpuBodyHandle handle) override;
            void QueueStep(SoftBodyGpuBodyHandle handle, float deltaTime) override;
            bool TryGetPositions(SoftBodyGpuBodyHandle handle, AZStd::vector<Vector3>& outPositions) override;

            // Pass-facing API (called by SoftBodyGpuSolverPass)...

            //! Incremented whenever the set of bodies changes and the packed buffers must be rebuilt.
            uint32_t GetGeneration() const;

            //! (Re)packs all registered bodies into concatenated GPU buffers. Called from the
            //! pass's BuildInternal, and safe to call when nothing changed.
            void PackBuffers();

            //! Fills the per-body params for this frame (consuming queued steps) and uploads them.
            //! Returns the number of packed bodies.
            uint32_t UpdateFrameParams();

            //! Returns a readback object that is ready to receive a new request, or null if all
            //! in-flight. Completion feeds TryGetPositions.
            AZStd::shared_ptr<RPI::AttachmentReadback> AcquireReadback();

            const Data::Instance<RPI::Buffer>& GetPositionsBuffer() const { return m_positionsBuffer; }
            const Data::Instance<RPI::Buffer>& GetPrevPositionsBuffer() const { return m_prevPositionsBuffer; }
            const Data::Instance<RPI::Buffer>& GetVelocitiesBuffer() const { return m_velocitiesBuffer; }
            const Data::Instance<RPI::Buffer>& GetConstraintCorrectionsBuffer() const { return m_constraintCorrectionsBuffer; }
            const Data::Instance<RPI::Buffer>& GetParticleGradientsBuffer() const { return m_particleGradientsBuffer; }
            const Data::Instance<RPI::Buffer>& GetTriangleGradientsBuffer() const { return m_triangleGradientsBuffer; }
            const Data::Instance<RPI::Buffer>& GetBodyParamsBuffer() const { return m_bodyParamsBuffer; }
            const Data::Instance<RPI::Buffer>& GetConstraintParticlesBuffer() const { return m_constraintParticlesBuffer; }
            const Data::Instance<RPI::Buffer>& GetConstraintParamsBuffer() const { return m_constraintParamsBuffer; }
            const Data::Instance<RPI::Buffer>& GetAdjacencyBuffer() const { return m_adjacencyBuffer; }
            const Data::Instance<RPI::Buffer>& GetAdjacencyOffsetsBuffer() const { return m_adjacencyOffsetsBuffer; }
            const Data::Instance<RPI::Buffer>& GetTrianglesBuffer() const { return m_trianglesBuffer; }
            const Data::Instance<RPI::Buffer>& GetTriAdjacencyBuffer() const { return m_triAdjacencyBuffer; }
            const Data::Instance<RPI::Buffer>& GetTriAdjacencyOffsetsBuffer() const { return m_triAdjacencyOffsetsBuffer; }

        private:
            struct BodyRecord
            {
                SoftBodyGpuBodyHandle m_handle = InvalidSoftBodyGpuBodyHandle;
                SoftBodyGpuBodyDesc m_desc;

                // Assigned by PackBuffers.
                uint32_t m_particleOffset = 0;
                uint32_t m_constraintOffset = 0;
                uint32_t m_triangleOffset = 0;

                // Frame state.
                float m_pendingDt = 0.0f;
                bool m_stepQueued = false;

                // Latest read-back world-space positions (empty until the first readback lands).
                AZStd::vector<Vector3> m_latestPositions;
            };

            void OnReadbackComplete(const RPI::AttachmentReadback::ReadbackResult& result);
            BodyRecord* FindBody(SoftBodyGpuBodyHandle handle);

            mutable AZStd::mutex m_mutex;
            AZStd::vector<BodyRecord> m_bodies;
            SoftBodyGpuBodyHandle m_nextHandle = 1;

            // Bumped on register/unregister; the pass rebuilds (and re-packs) when it lags behind.
            uint32_t m_generation = 1;
            uint32_t m_packedGeneration = 0;
            uint32_t m_packedParticleCount = 0;

            // GPU buffers (concatenated across bodies).
            Data::Instance<RPI::Buffer> m_positionsBuffer;
            Data::Instance<RPI::Buffer> m_prevPositionsBuffer;
            Data::Instance<RPI::Buffer> m_velocitiesBuffer;
            Data::Instance<RPI::Buffer> m_constraintCorrectionsBuffer;
            Data::Instance<RPI::Buffer> m_particleGradientsBuffer;
            Data::Instance<RPI::Buffer> m_triangleGradientsBuffer;
            Data::Instance<RPI::Buffer> m_bodyParamsBuffer;
            Data::Instance<RPI::Buffer> m_constraintParticlesBuffer;
            Data::Instance<RPI::Buffer> m_constraintParamsBuffer;
            Data::Instance<RPI::Buffer> m_adjacencyBuffer;
            Data::Instance<RPI::Buffer> m_adjacencyOffsetsBuffer;
            Data::Instance<RPI::Buffer> m_trianglesBuffer;
            Data::Instance<RPI::Buffer> m_triAdjacencyBuffer;
            Data::Instance<RPI::Buffer> m_triAdjacencyOffsetsBuffer;

            // Small ring of readback objects so a new readback can be issued (nearly) every frame
            // while earlier ones are still in flight.
            static constexpr uint32_t ReadbackCount = 3;
            AZStd::vector<AZStd::shared_ptr<RPI::AttachmentReadback>> m_readbacks;
        };
    } // namespace Render
} // namespace AZ
