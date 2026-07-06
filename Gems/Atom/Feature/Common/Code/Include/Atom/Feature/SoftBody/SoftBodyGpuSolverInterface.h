/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzCore/Math/Vector3.h>
#include <AzCore/Math/Vector4.h>
#include <AzCore/RTTI/RTTI.h>
#include <AzCore/std/containers/vector.h>

namespace AZ
{
    namespace Render
    {
        //! Handle identifying a soft body registered with the GPU solver. 0 is invalid.
        using SoftBodyGpuBodyHandle = uint64_t;
        inline constexpr SoftBodyGpuBodyHandle InvalidSoftBodyGpuBodyHandle = 0;

        //! Static description of a soft body for the GPU XPBD solver. Positions are world space.
        struct SoftBodyGpuBodyDesc
        {
            //! Per particle: xyz = world-space position, w = inverse mass (0 = pinned).
            AZStd::vector<Vector4> m_particles;

            //! Distance constraints: two particle indices per constraint (local to this body).
            AZStd::vector<uint32_t> m_constraintParticles;
            AZStd::vector<float> m_constraintRestLengths;
            AZStd::vector<float> m_constraintCompliances;

            //! Triangle list (3 indices per triangle) for the closed-mesh volume (pressure) constraint.
            AZStd::vector<uint32_t> m_triangles;
            float m_restVolume = 0.0f;

            // Simulation config (mirrors AZ::SoftBodyConfig).
            Vector3 m_gravity = Vector3(0.0f, 0.0f, -9.81f);
            uint32_t m_substeps = 4;
            uint32_t m_iterations = 4;
            float m_damping = 0.01f;
            bool m_groundPlaneEnabled = false;
            float m_groundHeight = 0.0f;
            float m_groundFriction = 0.5f;
            float m_pressure = 0.0f;
            float m_pressureCompliance = 0.0f;
        };

        //! Access point to the compute-shader XPBD soft body solver (registered via AZ::Interface).
        //! Bodies are simulated on the GPU by SoftBodyGpuSolverPass; positions are read back
        //! asynchronously, so TryGetPositions returns data a few frames behind the simulation.
        class SoftBodyGpuSolverInterface
        {
        public:
            AZ_RTTI(SoftBodyGpuSolverInterface, "{4B1E7C93-6A5D-4F82-B0C1-8D9E2F3A4B5C}");
            virtual ~SoftBodyGpuSolverInterface() = default;

            //! Registers a body for GPU simulation. Returns InvalidSoftBodyGpuBodyHandle on failure.
            virtual SoftBodyGpuBodyHandle RegisterBody(const SoftBodyGpuBodyDesc& desc) = 0;

            //! Removes a body from the GPU simulation.
            virtual void UnregisterBody(SoftBodyGpuBodyHandle handle) = 0;

            //! Queues one simulation step of \p deltaTime seconds for the next rendered frame.
            virtual void QueueStep(SoftBodyGpuBodyHandle handle, float deltaTime) = 0;

            //! Copies the latest read-back world-space particle positions into \p outPositions.
            //! Returns false while no readback has completed yet (the first few frames).
            virtual bool TryGetPositions(SoftBodyGpuBodyHandle handle, AZStd::vector<Vector3>& outPositions) = 0;
        };
    } // namespace Render
} // namespace AZ
