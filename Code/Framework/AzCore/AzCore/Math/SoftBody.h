/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Math/Vector3.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/functional.h>

namespace AZ
{
    //! A particle in a soft body simulation. Inverse mass of 0 pins the particle in place.
    struct SoftBodyParticle
    {
        Vector3 m_position = Vector3::CreateZero();
        Vector3 m_prevPosition = Vector3::CreateZero();
        Vector3 m_velocity = Vector3::CreateZero();
        float m_invMass = 1.0f;
    };

    //! XPBD compliant distance (stretch) constraint between two particles.
    struct SoftBodyDistanceConstraint
    {
        uint32_t m_particleA = 0;
        uint32_t m_particleB = 0;
        float m_restLength = 0.0f;
        float m_compliance = 0.0f; //!< 0 = rigid; larger = softer (meters/Newton).
    };

    //! XPBD tetrahedral volume constraint (4 particles keep their signed tet volume).
    struct SoftBodyTetVolumeConstraint
    {
        uint32_t m_particles[4] = { 0, 0, 0, 0 };
        float m_restVolume = 0.0f;
        float m_compliance = 0.0f;
    };

    //! Simulation-wide settings.
    struct SoftBodyConfig
    {
        Vector3 m_gravity = Vector3(0.0f, 0.0f, -9.81f);
        uint32_t m_substeps = 4;      //!< XPBD small-substeps; more = stiffer and more accurate.
        uint32_t m_iterations = 4;    //!< Constraint solver iterations per substep.
        float m_damping = 0.01f;      //!< Per-second velocity damping factor [0..1].
        bool m_groundPlaneEnabled = false;
        float m_groundHeight = 0.0f;  //!< World-space Z of the ground collision plane.
        float m_groundFriction = 0.5f; //!< Tangential velocity scale on ground contact [0..1].
        //! Global volume (pressure) constraint for closed meshes: 0 = off, 1 = preserve rest
        //! volume, > 1 = over-pressurized balloon.
        float m_pressure = 0.0f;
        float m_pressureCompliance = 0.0f;
    };

    //! Minimal XPBD (extended position-based dynamics) soft body: particles + compliant
    //! distance / volume constraints, integrated with small substeps. Deterministic, portable
    //! and unconditionally stable. Build one from a triangle mesh with BuildFromTriangleMesh
    //! or assemble particles/constraints manually.
    class AZCORE_API SoftBody
    {
    public:
        SoftBody() = default;

        //! Builds particles from \p positions, a distance constraint per unique mesh edge and
        //! (when \p config.m_pressure > 0) a global volume constraint from the closed mesh.
        //! \p indices is a triangle list (3 indices per triangle). \p massPerParticle <= 0 pins all particles.
        void BuildFromTriangleMesh(
            const AZStd::vector<Vector3>& positions,
            const AZStd::vector<uint32_t>& indices,
            float massPerParticle,
            float compliance,
            const SoftBodyConfig& config);

        uint32_t AddParticle(const Vector3& position, float invMass);
        void AddDistanceConstraint(uint32_t particleA, uint32_t particleB, float compliance);
        void AddTetVolumeConstraint(uint32_t a, uint32_t b, uint32_t c, uint32_t d, float compliance);

        //! Advances the simulation by \p deltaTime seconds (internally split into substeps).
        void Step(float deltaTime);

        //! Optional external collision hook, invoked once per substep after the built-in ground
        //! contacts. The callback receives the particle array and the substep delta time, and may
        //! project particle positions out of colliding geometry (positions only; velocities are
        //! derived afterwards).
        using CollisionSolver = AZStd::function<void(AZStd::vector<SoftBodyParticle>&, float dt)>;
        void SetCollisionSolver(CollisionSolver solver) { m_collisionSolver = AZStd::move(solver); }

        void SetConfig(const SoftBodyConfig& config) { m_config = config; }
        const SoftBodyConfig& GetConfig() const { return m_config; }

        AZStd::vector<SoftBodyParticle>& GetParticles() { return m_particles; }
        const AZStd::vector<SoftBodyParticle>& GetParticles() const { return m_particles; }
        const AZStd::vector<SoftBodyDistanceConstraint>& GetDistanceConstraints() const { return m_distanceConstraints; }

        //! Signed volume of the closed triangle mesh the body was built from (0 if not mesh-built).
        float ComputeMeshVolume() const;

        //! Center of mass of all particles (unweighted average for pinned-free mixes with equal masses).
        Vector3 ComputeCenter() const;

        //! Resolves contacts between the particles of two different soft bodies: any particle of
        //! \p bodyA closer than \p radiusA + \p radiusB to a particle of \p bodyB is projected out
        //! along the separation axis, with tangential \p friction [0..1] applied on contact.
        //! The response is one-sided (only \p bodyA moves); mutual collision comes from each body
        //! running this against the other during its own step. Uses a spatial hash over \p bodyB.
        static void SolveParticleContacts(
            AZStd::vector<SoftBodyParticle>& bodyA, float radiusA,
            AZStd::vector<SoftBodyParticle>& bodyB, float radiusB,
            float friction);

    private:
        void SubStep(float dt);
        void SolveDistanceConstraints(float dt);
        void SolveTetVolumeConstraints(float dt);
        void SolveMeshVolumeConstraint(float dt);
        void SolveGroundContacts();

        SoftBodyConfig m_config;
        CollisionSolver m_collisionSolver;
        AZStd::vector<SoftBodyParticle> m_particles;
        AZStd::vector<SoftBodyDistanceConstraint> m_distanceConstraints;
        AZStd::vector<SoftBodyTetVolumeConstraint> m_tetVolumeConstraints;

        // Closed-mesh global volume (pressure) constraint state.
        AZStd::vector<uint32_t> m_meshIndices;
        float m_restMeshVolume = 0.0f;
    };
} // namespace AZ
