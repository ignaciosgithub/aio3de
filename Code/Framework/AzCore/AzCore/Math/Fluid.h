/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Math/Aabb.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/functional.h>

namespace AZ
{
    //! A particle in a fluid simulation.
    struct FluidParticle
    {
        Vector3 m_position = Vector3::CreateZero();
        Vector3 m_prevPosition = Vector3::CreateZero();
        Vector3 m_velocity = Vector3::CreateZero();
    };

    //! Simulation-wide fluid settings. Viscosity spans the full liquid range: ~0.01 behaves
    //! like water, ~1 like honey/syrup.
    struct FluidConfig
    {
        Vector3 m_gravity = Vector3(0.0f, 0.0f, -9.81f);
        float m_restDensity = 1000.0f;    //!< Target density in kg/m^3 (water = 1000, honey ~ 1400).
        float m_particleSpacing = 0.1f;   //!< Rest distance between particles in meters (sets resolution and cost).
        uint32_t m_substeps = 2;          //!< Simulation substeps per Step call.
        uint32_t m_iterations = 3;        //!< Density-constraint solver iterations per substep.
        float m_viscosity = 0.02f;        //!< XSPH viscosity [0..1]: 0.01-0.05 water, 0.5-1 honey.
        float m_damping = 0.0f;           //!< Extra per-second velocity damping [0..1].
        bool m_boundsEnabled = true;      //!< Keep particles inside m_bounds (container box).
        Aabb m_bounds = Aabb::CreateFromMinMax(Vector3(-5.0f, -5.0f, 0.0f), Vector3(5.0f, 5.0f, 10.0f));
        float m_boundsRestitution = 0.0f; //!< Velocity bounce on container contact [0..1].
        bool m_parallel = true;           //!< Solve per-particle loops on worker threads (requires a TaskExecutor).
    };

    //! Position based fluids (Macklin & Muller PBF): an incompressible SPH fluid solved as a
    //! per-particle density constraint, unconditionally stable at game timesteps. One solver
    //! covers the whole liquid range from water to honey via the viscosity setting; wind/air
    //! is better served by the analytic WindField below. Deterministic and portable (CPU).
    class AZCORE_API FluidSim
    {
    public:
        FluidSim() = default;

        //! Fills the box [min, max] with particles on a regular grid at the configured spacing.
        //! Existing particles are kept; returns the number of particles added.
        uint32_t SpawnBox(const Vector3& boxMin, const Vector3& boxMax);

        uint32_t AddParticle(const Vector3& position, const Vector3& velocity = Vector3::CreateZero());
        void Clear();

        //! Advances the simulation by \p deltaTime seconds (internally split into substeps).
        void Step(float deltaTime);

        //! Optional per-particle external acceleration (e.g. wind drag), sampled at the particle
        //! position during force integration. Must be thread-safe when m_parallel is enabled.
        using AccelerationField = AZStd::function<Vector3(const Vector3& position, const Vector3& velocity)>;
        void SetExternalAcceleration(AccelerationField field) { m_externalAcceleration = AZStd::move(field); }

        void SetConfig(const FluidConfig& config);
        const FluidConfig& GetConfig() const { return m_config; }

        AZStd::vector<FluidParticle>& GetParticles() { return m_particles; }
        const AZStd::vector<FluidParticle>& GetParticles() const { return m_particles; }

        //! Mass of one particle in kg, derived from rest density and spacing.
        float GetParticleMass() const { return m_particleMass; }

        //! SPH smoothing radius in meters, derived from the particle spacing.
        float GetSmoothingRadius() const { return m_smoothingRadius; }

        //! Average SPH density over all particles (diagnostics; approaches rest density at equilibrium).
        float ComputeAverageDensity() const;

    private:
        void SubStep(float dt);
        void BuildNeighborGrid();
        void ForEachParticle(uint32_t count, const AZStd::function<void(uint32_t)>& kernel) const;
        void UpdateDerivedQuantities();

        FluidConfig m_config;
        AccelerationField m_externalAcceleration;
        AZStd::vector<FluidParticle> m_particles;

        // Derived kernel constants (updated when the config changes).
        float m_particleMass = 1.0f;
        float m_smoothingRadius = 0.2f;
        float m_poly6Coeff = 0.0f;
        float m_spikyGradCoeff = 0.0f;

        // Scratch buffers reused across substeps.
        AZStd::vector<Vector3> m_predicted;
        AZStd::vector<float> m_lambdas;
        AZStd::vector<Vector3> m_deltas;
        AZStd::vector<uint32_t> m_cellStart;   //!< Compact hash grid: cell -> first index in m_cellEntries.
        AZStd::vector<uint32_t> m_cellEntries; //!< Particle indices sorted by cell.
        AZStd::vector<uint64_t> m_cellKeys;
        AZStd::vector<AZStd::pair<uint64_t, uint32_t>> m_sortScratch;
    };

    //! Procedural wind: an analytic, divergence-friendly velocity field that can be sampled at
    //! any world position for a given time. Combines a base direction with periodic gusts and
    //! spatial turbulence, so wind stays inexpensive (no particles or grids) while still feeling
    //! alive. Feed the sample into FluidSim::SetExternalAcceleration as a drag force, into soft
    //! bodies, or directly onto rigid bodies.
    class AZCORE_API WindField
    {
    public:
        struct Config
        {
            Vector3 m_baseVelocity = Vector3(5.0f, 0.0f, 0.0f); //!< Steady wind velocity in m/s.
            float m_gustStrength = 0.5f;     //!< Gust amplitude as a fraction of the base speed [0..1+].
            float m_gustFrequency = 0.3f;    //!< Gusts per second.
            float m_turbulence = 0.25f;      //!< Spatial turbulence amplitude as a fraction of base speed.
            float m_turbulenceScale = 4.0f;  //!< Size of turbulence eddies in meters.
        };

        WindField() = default;
        explicit WindField(const Config& config) : m_config(config) {}

        void SetConfig(const Config& config) { m_config = config; }
        const Config& GetConfig() const { return m_config; }

        //! Wind velocity in m/s at \p position and \p time. Deterministic and thread-safe.
        Vector3 Sample(const Vector3& position, float time) const;

        //! Acceleration a body immersed in this wind experiences, using a linear drag model:
        //! a = (wind - bodyVelocity) * dragCoefficient.
        Vector3 DragAcceleration(const Vector3& position, const Vector3& bodyVelocity, float time, float dragCoefficient) const;

    private:
        Config m_config;
    };
} // namespace AZ
