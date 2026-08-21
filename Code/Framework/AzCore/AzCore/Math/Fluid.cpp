/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Math/Fluid.h>

#include <AzCore/Math/MathUtils.h>
#include <AzCore/Task/Algorithms.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/sort.h>

namespace AZ
{
    namespace
    {
        constexpr float Epsilon = 1e-9f;
        constexpr float Pi = Constants::Pi;

        // Relaxation for the CFM term in the lambda denominator (PBF eq. 11).
        constexpr float RelaxationEpsilon = 100.0f;

        // Artificial pressure (PBF eq. 13) keeps particles from clumping and adds
        // surface-tension-like cohesion.
        constexpr float ArtificialPressureStrength = 0.0001f;
        constexpr int ArtificialPressurePower = 4;
        constexpr float ArtificialPressureRadiusFraction = 0.2f;

        uint64_t SpatialCellKey(int32_t x, int32_t y, int32_t z)
        {
            // 21 bits per axis, offset to keep negative coordinates distinct.
            constexpr uint64_t Offset = 1ull << 20;
            constexpr uint64_t Mask = (1ull << 21) - 1;
            return (((static_cast<uint64_t>(x) + Offset) & Mask) << 42) |
                (((static_cast<uint64_t>(y) + Offset) & Mask) << 21) |
                ((static_cast<uint64_t>(z) + Offset) & Mask);
        }
    } // namespace

    void FluidSim::SetConfig(const FluidConfig& config)
    {
        m_config = config;
        UpdateDerivedQuantities();
    }

    void FluidSim::UpdateDerivedQuantities()
    {
        const float spacing = AZStd::max(m_config.m_particleSpacing, 1e-4f);
        m_particleMass = m_config.m_restDensity * spacing * spacing * spacing;
        m_smoothingRadius = 2.0f * spacing;

        const float h = m_smoothingRadius;
        const float h3 = h * h * h;
        m_poly6Coeff = 315.0f / (64.0f * Pi * h3 * h3 * h3);
        m_spikyGradCoeff = -45.0f / (Pi * h3 * h3);
    }

    uint32_t FluidSim::AddParticle(const Vector3& position, const Vector3& velocity)
    {
        FluidParticle particle;
        particle.m_position = position;
        particle.m_prevPosition = position;
        particle.m_velocity = velocity;
        m_particles.push_back(particle);
        return static_cast<uint32_t>(m_particles.size() - 1);
    }

    uint32_t FluidSim::SpawnBox(const Vector3& boxMin, const Vector3& boxMax)
    {
        UpdateDerivedQuantities();
        const float spacing = AZStd::max(m_config.m_particleSpacing, 1e-4f);
        uint32_t added = 0;
        for (float z = boxMin.GetZ(); z <= boxMax.GetZ() + Epsilon; z += spacing)
        {
            for (float y = boxMin.GetY(); y <= boxMax.GetY() + Epsilon; y += spacing)
            {
                for (float x = boxMin.GetX(); x <= boxMax.GetX() + Epsilon; x += spacing)
                {
                    AddParticle(Vector3(x, y, z));
                    ++added;
                }
            }
        }
        return added;
    }

    void FluidSim::Clear()
    {
        m_particles.clear();
    }

    void FluidSim::ForEachParticle(uint32_t count, const AZStd::function<void(uint32_t)>& kernel) const
    {
        if (m_config.m_parallel)
        {
            static constexpr TaskDescriptor FluidTaskDescriptor{ "FluidSim", "Fluid" };
            ParallelFor(uint32_t{ 0 }, count, kernel, FluidTaskDescriptor);
        }
        else
        {
            for (uint32_t i = 0; i < count; ++i)
            {
                kernel(i);
            }
        }
    }

    void FluidSim::BuildNeighborGrid()
    {
        const uint32_t count = static_cast<uint32_t>(m_particles.size());
        const float invCellSize = 1.0f / m_smoothingRadius;

        m_sortScratch.resize(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            const Vector3& p = m_predicted[i];
            const int32_t cx = static_cast<int32_t>(AZStd::floorf(p.GetX() * invCellSize));
            const int32_t cy = static_cast<int32_t>(AZStd::floorf(p.GetY() * invCellSize));
            const int32_t cz = static_cast<int32_t>(AZStd::floorf(p.GetZ() * invCellSize));
            m_sortScratch[i] = { SpatialCellKey(cx, cy, cz), i };
        }
        AZStd::sort(
            m_sortScratch.begin(), m_sortScratch.end(),
            [](const AZStd::pair<uint64_t, uint32_t>& a, const AZStd::pair<uint64_t, uint32_t>& b)
            {
                return a.first < b.first;
            });

        m_cellEntries.resize(count);
        m_cellKeys.resize(count);
        m_cellStart.assign(count + 1, 0);
        for (uint32_t i = 0; i < count; ++i)
        {
            m_cellEntries[i] = m_sortScratch[i].second;
            m_cellKeys[i] = m_sortScratch[i].first;
        }
    }

    template<typename Visitor>
    static void VisitNeighbors(
        const AZStd::vector<Vector3>& predicted,
        const AZStd::vector<uint64_t>& cellKeys,
        const AZStd::vector<uint32_t>& cellEntries,
        const Vector3& position,
        float cellSize,
        const Visitor& visitor)
    {
        const float invCellSize = 1.0f / cellSize;
        const int32_t cx = static_cast<int32_t>(AZStd::floorf(position.GetX() * invCellSize));
        const int32_t cy = static_cast<int32_t>(AZStd::floorf(position.GetY() * invCellSize));
        const int32_t cz = static_cast<int32_t>(AZStd::floorf(position.GetZ() * invCellSize));

        for (int32_t dz = -1; dz <= 1; ++dz)
        {
            for (int32_t dy = -1; dy <= 1; ++dy)
            {
                for (int32_t dx = -1; dx <= 1; ++dx)
                {
                    const uint64_t key = SpatialCellKey(cx + dx, cy + dy, cz + dz);
                    auto begin = AZStd::lower_bound(cellKeys.begin(), cellKeys.end(), key);
                    for (auto it = begin; it != cellKeys.end() && *it == key; ++it)
                    {
                        const uint32_t neighborIndex = cellEntries[it - cellKeys.begin()];
                        visitor(neighborIndex, predicted[neighborIndex]);
                    }
                }
            }
        }
    }

    void FluidSim::Step(float deltaTime)
    {
        if (m_particles.empty() || deltaTime <= 0.0f)
        {
            return;
        }
        UpdateDerivedQuantities();

        const uint32_t substeps = AZStd::max(m_config.m_substeps, 1u);
        const float dt = deltaTime / static_cast<float>(substeps);
        for (uint32_t s = 0; s < substeps; ++s)
        {
            SubStep(dt);
        }
    }

    void FluidSim::SubStep(float dt)
    {
        const uint32_t count = static_cast<uint32_t>(m_particles.size());
        const float h = m_smoothingRadius;
        const float h2 = h * h;
        const float restDensity = AZStd::max(m_config.m_restDensity, 1.0f);
        const float invRestDensity = 1.0f / restDensity;
        const float mass = m_particleMass;

        m_predicted.resize(count);
        m_lambdas.resize(count);
        m_deltas.resize(count);

        // 1. Integrate external forces and predict positions.
        ForEachParticle(
            count,
            [&](uint32_t i)
            {
                FluidParticle& particle = m_particles[i];
                Vector3 acceleration = m_config.m_gravity;
                if (m_externalAcceleration)
                {
                    acceleration += m_externalAcceleration(particle.m_position, particle.m_velocity);
                }
                particle.m_velocity += acceleration * dt;
                m_predicted[i] = particle.m_position + particle.m_velocity * dt;
            });

        BuildNeighborGrid();

        // Precomputed artificial-pressure denominator W(dq) (PBF eq. 13).
        const float dq = ArtificialPressureRadiusFraction * h;
        const float wDq = m_poly6Coeff * powf(h2 - dq * dq, 3.0f);
        const float invWDq = wDq > Epsilon ? 1.0f / wDq : 0.0f;

        const uint32_t iterations = AZStd::max(m_config.m_iterations, 1u);
        for (uint32_t iter = 0; iter < iterations; ++iter)
        {
            // 2. Compute per-particle density constraint lambdas (PBF eq. 9-11).
            ForEachParticle(
                count,
                [&](uint32_t i)
                {
                    const Vector3& pi = m_predicted[i];
                    float density = 0.0f;
                    Vector3 gradSelf = Vector3::CreateZero();
                    float gradSumSq = 0.0f;
                    VisitNeighbors(
                        m_predicted, m_cellKeys, m_cellEntries, pi, h,
                        [&](uint32_t j, const Vector3& pj)
                        {
                            const Vector3 diff = pi - pj;
                            const float r2 = diff.GetLengthSq();
                            if (r2 >= h2)
                            {
                                return;
                            }
                            const float w = h2 - r2;
                            density += mass * m_poly6Coeff * w * w * w;
                            if (j != i && r2 > Epsilon)
                            {
                                const float r = sqrtf(r2);
                                const float hr = h - r;
                                const Vector3 grad = diff * (m_spikyGradCoeff * hr * hr / r) * (mass * invRestDensity);
                                gradSelf += grad;
                                gradSumSq += grad.GetLengthSq();
                            }
                        });

                    const float constraint = density * invRestDensity - 1.0f;
                    if (constraint > 0.0f)
                    {
                        const float denom = gradSumSq + gradSelf.GetLengthSq() + RelaxationEpsilon;
                        m_lambdas[i] = -constraint / denom;
                    }
                    else
                    {
                        m_lambdas[i] = 0.0f;
                    }
                });

            // 3. Compute and apply position corrections (PBF eq. 12-14).
            ForEachParticle(
                count,
                [&](uint32_t i)
                {
                    const Vector3& pi = m_predicted[i];
                    Vector3 delta = Vector3::CreateZero();
                    VisitNeighbors(
                        m_predicted, m_cellKeys, m_cellEntries, pi, h,
                        [&](uint32_t j, const Vector3& pj)
                        {
                            if (j == i)
                            {
                                return;
                            }
                            const Vector3 diff = pi - pj;
                            const float r2 = diff.GetLengthSq();
                            if (r2 >= h2 || r2 <= Epsilon)
                            {
                                return;
                            }
                            const float r = sqrtf(r2);
                            const float hr = h - r;
                            const float w = h2 - r2;
                            const float wPoly = m_poly6Coeff * w * w * w;
                            float sCorr = 0.0f;
                            if (invWDq > 0.0f)
                            {
                                float ratio = wPoly * invWDq;
                                float ratioPow = 1.0f;
                                for (int p = 0; p < ArtificialPressurePower; ++p)
                                {
                                    ratioPow *= ratio;
                                }
                                sCorr = -ArtificialPressureStrength * ratioPow;
                            }
                            const Vector3 grad = diff * (m_spikyGradCoeff * hr * hr / r);
                            delta += grad * ((m_lambdas[i] + m_lambdas[j] + sCorr) * mass * invRestDensity);
                        });
                    m_deltas[i] = delta;
                });

            ForEachParticle(
                count,
                [&](uint32_t i)
                {
                    Vector3 p = m_predicted[i] + m_deltas[i];
                    if (m_config.m_boundsEnabled)
                    {
                        p = p.GetClamp(m_config.m_bounds.GetMin(), m_config.m_bounds.GetMax());
                    }
                    m_predicted[i] = p;
                });
        }

        // 4. Update velocities from positions, apply container restitution and damping.
        const float invDt = 1.0f / dt;
        const float dampingFactor = AZStd::clamp(1.0f - m_config.m_damping * dt, 0.0f, 1.0f);
        ForEachParticle(
            count,
            [&](uint32_t i)
            {
                FluidParticle& particle = m_particles[i];
                particle.m_prevPosition = particle.m_position;
                particle.m_velocity = (m_predicted[i] - particle.m_position) * invDt * dampingFactor;

                if (m_config.m_boundsEnabled)
                {
                    const Vector3& boundsMin = m_config.m_bounds.GetMin();
                    const Vector3& boundsMax = m_config.m_bounds.GetMax();
                    const float restitution = -m_config.m_boundsRestitution;
                    Vector3 v = particle.m_velocity;
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        const float p = m_predicted[i].GetElement(axis);
                        const float vel = v.GetElement(axis);
                        if ((p <= boundsMin.GetElement(axis) + Epsilon && vel < 0.0f) ||
                            (p >= boundsMax.GetElement(axis) - Epsilon && vel > 0.0f))
                        {
                            v.SetElement(axis, vel * restitution);
                        }
                    }
                    particle.m_velocity = v;
                }
            });

        // 5. XSPH viscosity (PBF eq. 17): blend each particle's velocity toward its neighborhood
        // average. Low values give watery flow; values near 1 give thick honey-like motion.
        const float viscosity = AZStd::clamp(m_config.m_viscosity, 0.0f, 1.0f);
        if (viscosity > 0.0f)
        {
            m_deltas.resize(count);
            ForEachParticle(
                count,
                [&](uint32_t i)
                {
                    const Vector3& pi = m_predicted[i];
                    const Vector3& vi = m_particles[i].m_velocity;
                    Vector3 velocityCorrection = Vector3::CreateZero();
                    VisitNeighbors(
                        m_predicted, m_cellKeys, m_cellEntries, pi, h,
                        [&](uint32_t j, const Vector3& pj)
                        {
                            if (j == i)
                            {
                                return;
                            }
                            const float r2 = (pi - pj).GetLengthSq();
                            if (r2 >= h2)
                            {
                                return;
                            }
                            const float w = h2 - r2;
                            const float wPoly = m_poly6Coeff * w * w * w;
                            velocityCorrection += (m_particles[j].m_velocity - vi) * (wPoly * mass * invRestDensity);
                        });
                    m_deltas[i] = velocityCorrection * viscosity;
                });
            ForEachParticle(
                count,
                [&](uint32_t i)
                {
                    m_particles[i].m_velocity += m_deltas[i];
                });
        }

        ForEachParticle(
            count,
            [&](uint32_t i)
            {
                m_particles[i].m_position = m_predicted[i];
            });
    }

    float FluidSim::ComputeAverageDensity() const
    {
        const uint32_t count = static_cast<uint32_t>(m_particles.size());
        if (count == 0)
        {
            return 0.0f;
        }
        const float h = m_smoothingRadius;
        const float h2 = h * h;
        float total = 0.0f;
        for (uint32_t i = 0; i < count; ++i)
        {
            const Vector3& pi = m_particles[i].m_position;
            float density = 0.0f;
            for (uint32_t j = 0; j < count; ++j)
            {
                const float r2 = (pi - m_particles[j].m_position).GetLengthSq();
                if (r2 < h2)
                {
                    const float w = h2 - r2;
                    density += m_particleMass * m_poly6Coeff * w * w * w;
                }
            }
            total += density;
        }
        return total / static_cast<float>(count);
    }

    Vector3 WindField::Sample(const Vector3& position, float time) const
    {
        const Vector3& base = m_config.m_baseVelocity;
        const float baseSpeed = base.GetLength();
        if (baseSpeed < Epsilon)
        {
            return Vector3::CreateZero();
        }
        const Vector3 direction = base / baseSpeed;

        // Temporal gusts: two incommensurate sine waves so the pattern does not visibly repeat.
        const float gustPhase = 2.0f * Pi * m_config.m_gustFrequency * time;
        const float gust = m_config.m_gustStrength * baseSpeed *
            (0.65f * sinf(gustPhase) + 0.35f * sinf(2.7f * gustPhase + 1.3f));

        // Spatial turbulence: cheap trigonometric noise, phase-shifted per axis so the
        // perturbation direction varies over space instead of only its magnitude.
        Vector3 turbulenceOffset = Vector3::CreateZero();
        const float turbulence = m_config.m_turbulence * baseSpeed;
        if (turbulence > Epsilon)
        {
            const float invScale = 1.0f / AZStd::max(m_config.m_turbulenceScale, 0.01f);
            const float px = position.GetX() * invScale;
            const float py = position.GetY() * invScale;
            const float pz = position.GetZ() * invScale;
            const float t = 0.5f * time;
            turbulenceOffset = Vector3(
                sinf(py * 1.7f + pz * 0.9f + t * 1.1f),
                sinf(pz * 1.3f + px * 1.9f + t * 0.7f + 2.1f),
                0.35f * sinf(px * 1.1f + py * 1.5f + t * 0.9f + 4.2f)) * turbulence;
        }

        return direction * (baseSpeed + gust) + turbulenceOffset;
    }

    Vector3 WindField::DragAcceleration(
        const Vector3& position, const Vector3& bodyVelocity, float time, float dragCoefficient) const
    {
        return (Sample(position, time) - bodyVelocity) * dragCoefficient;
    }
} // namespace AZ
