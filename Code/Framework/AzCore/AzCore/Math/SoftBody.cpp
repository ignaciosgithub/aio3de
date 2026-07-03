/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Math/SoftBody.h>

#include <AzCore/std/algorithm.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/containers/unordered_set.h>
#include <AzCore/std/utility/pair.h>

namespace AZ
{
    namespace
    {
        constexpr float Epsilon = 1e-9f;

        uint64_t EdgeKey(uint32_t a, uint32_t b)
        {
            const uint32_t lo = a < b ? a : b;
            const uint32_t hi = a < b ? b : a;
            return (static_cast<uint64_t>(lo) << 32) | hi;
        }

        float SignedTetVolume(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d)
        {
            return (b - a).Cross(c - a).Dot(d - a) / 6.0f;
        }

        int64_t SpatialCellKey(int32_t x, int32_t y, int32_t z)
        {
            // 21 bits per axis, offset to keep negative coordinates distinct.
            constexpr int64_t Offset = 1 << 20;
            constexpr int64_t Mask = (1 << 21) - 1;
            return (((x + Offset) & Mask) << 42) | (((y + Offset) & Mask) << 21) | ((z + Offset) & Mask);
        }
    } // namespace

    void SoftBody::BuildFromTriangleMesh(
        const AZStd::vector<Vector3>& positions,
        const AZStd::vector<uint32_t>& indices,
        float massPerParticle,
        float compliance,
        const SoftBodyConfig& config)
    {
        m_config = config;
        m_particles.clear();
        m_distanceConstraints.clear();
        m_tetVolumeConstraints.clear();
        m_meshIndices = indices;

        const float invMass = massPerParticle > 0.0f ? 1.0f / massPerParticle : 0.0f;
        m_particles.reserve(positions.size());
        for (const Vector3& position : positions)
        {
            AddParticle(position, invMass);
        }

        AZStd::unordered_set<uint64_t> edges;
        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const uint32_t tri[3] = { indices[i], indices[i + 1], indices[i + 2] };
            for (int e = 0; e < 3; ++e)
            {
                const uint32_t a = tri[e];
                const uint32_t b = tri[(e + 1) % 3];
                if (a < m_particles.size() && b < m_particles.size() && edges.insert(EdgeKey(a, b)).second)
                {
                    AddDistanceConstraint(a, b, compliance);
                }
            }
        }

        m_restMeshVolume = ComputeMeshVolume();
    }

    uint32_t SoftBody::AddParticle(const Vector3& position, float invMass)
    {
        SoftBodyParticle particle;
        particle.m_position = position;
        particle.m_prevPosition = position;
        particle.m_invMass = invMass;
        m_particles.push_back(particle);
        return static_cast<uint32_t>(m_particles.size() - 1);
    }

    void SoftBody::AddDistanceConstraint(uint32_t particleA, uint32_t particleB, float compliance)
    {
        SoftBodyDistanceConstraint constraint;
        constraint.m_particleA = particleA;
        constraint.m_particleB = particleB;
        constraint.m_restLength = (m_particles[particleB].m_position - m_particles[particleA].m_position).GetLength();
        constraint.m_compliance = compliance;
        m_distanceConstraints.push_back(constraint);
    }

    void SoftBody::AddTetVolumeConstraint(uint32_t a, uint32_t b, uint32_t c, uint32_t d, float compliance)
    {
        SoftBodyTetVolumeConstraint constraint;
        constraint.m_particles[0] = a;
        constraint.m_particles[1] = b;
        constraint.m_particles[2] = c;
        constraint.m_particles[3] = d;
        constraint.m_restVolume = SignedTetVolume(
            m_particles[a].m_position, m_particles[b].m_position, m_particles[c].m_position, m_particles[d].m_position);
        constraint.m_compliance = compliance;
        m_tetVolumeConstraints.push_back(constraint);
    }

    float SoftBody::ComputeMeshVolume() const
    {
        float volume = 0.0f;
        for (size_t i = 0; i + 2 < m_meshIndices.size(); i += 3)
        {
            const Vector3& a = m_particles[m_meshIndices[i]].m_position;
            const Vector3& b = m_particles[m_meshIndices[i + 1]].m_position;
            const Vector3& c = m_particles[m_meshIndices[i + 2]].m_position;
            volume += a.Cross(b).Dot(c) / 6.0f;
        }
        return volume;
    }

    Vector3 SoftBody::ComputeCenter() const
    {
        Vector3 center = Vector3::CreateZero();
        if (m_particles.empty())
        {
            return center;
        }
        for (const SoftBodyParticle& particle : m_particles)
        {
            center += particle.m_position;
        }
        return center / static_cast<float>(m_particles.size());
    }

    void SoftBody::SolveParticleContacts(
        AZStd::vector<SoftBodyParticle>& bodyA, float radiusA,
        AZStd::vector<SoftBodyParticle>& bodyB, float radiusB,
        float friction)
    {
        if (bodyA.empty() || bodyB.empty())
        {
            return;
        }
        const float contactDistance = radiusA + radiusB;
        if (contactDistance <= 0.0f)
        {
            return;
        }
        const float invCellSize = 1.0f / contactDistance;
        const float contactDistanceSq = contactDistance * contactDistance;
        const float clampedFriction = AZStd::clamp(friction, 0.0f, 1.0f);

        // Hash bodyB's particles into cells sized to the contact distance.
        AZStd::unordered_map<int64_t, AZStd::vector<uint32_t>> grid;
        grid.reserve(bodyB.size());
        for (uint32_t i = 0; i < bodyB.size(); ++i)
        {
            const Vector3& p = bodyB[i].m_position;
            grid[SpatialCellKey(
                     static_cast<int32_t>(AZStd::floorf(p.GetX() * invCellSize)),
                     static_cast<int32_t>(AZStd::floorf(p.GetY() * invCellSize)),
                     static_cast<int32_t>(AZStd::floorf(p.GetZ() * invCellSize)))]
                .push_back(i);
        }

        for (SoftBodyParticle& particleA : bodyA)
        {
            if (particleA.m_invMass <= 0.0f)
            {
                continue;
            }
            const Vector3& pa = particleA.m_position;
            const int32_t cx = static_cast<int32_t>(AZStd::floorf(pa.GetX() * invCellSize));
            const int32_t cy = static_cast<int32_t>(AZStd::floorf(pa.GetY() * invCellSize));
            const int32_t cz = static_cast<int32_t>(AZStd::floorf(pa.GetZ() * invCellSize));
            for (int32_t dx = -1; dx <= 1; ++dx)
            {
                for (int32_t dy = -1; dy <= 1; ++dy)
                {
                    for (int32_t dz = -1; dz <= 1; ++dz)
                    {
                        auto cell = grid.find(SpatialCellKey(cx + dx, cy + dy, cz + dz));
                        if (cell == grid.end())
                        {
                            continue;
                        }
                        for (uint32_t indexB : cell->second)
                        {
                            // One-sided response: only this body's particles move. The other body
                            // resolves its own side of the contact during its own step, which keeps
                            // the pair from pumping energy into each other.
                            const SoftBodyParticle& particleB = bodyB[indexB];
                            Vector3 delta = particleA.m_position - particleB.m_position;
                            const float distanceSq = delta.GetLengthSq();
                            if (distanceSq >= contactDistanceSq || distanceSq < Epsilon)
                            {
                                continue;
                            }
                            const float distance = Sqrt(distanceSq);
                            Vector3 normal = delta / distance;
                            float penetration = contactDistance - distance;
                            // If the pair crossed sides during this substep, the current separation
                            // axis points the wrong way; resolve toward the pre-substep side instead
                            // of pushing the particles through each other.
                            const Vector3 prevDelta = particleA.m_prevPosition - particleB.m_prevPosition;
                            if (normal.Dot(prevDelta) < 0.0f)
                            {
                                normal = -normal;
                                penetration = contactDistance + distance;
                            }
                            particleA.m_position += normal * penetration;

                            // Friction: damp the relative tangential motion of this substep.
                            const Vector3 relativeMotion =
                                (particleA.m_position - particleA.m_prevPosition) -
                                (particleB.m_position - particleB.m_prevPosition);
                            const Vector3 tangential = relativeMotion - normal * relativeMotion.Dot(normal);
                            particleA.m_position -= tangential * clampedFriction;
                        }
                    }
                }
            }
        }
    }

    void SoftBody::Step(float deltaTime)
    {
        if (deltaTime <= 0.0f || m_particles.empty())
        {
            return;
        }

        const uint32_t substeps = m_config.m_substeps > 0 ? m_config.m_substeps : 1;
        const float dt = deltaTime / static_cast<float>(substeps);
        for (uint32_t i = 0; i < substeps; ++i)
        {
            SubStep(dt);
        }
    }

    void SoftBody::SubStep(float dt)
    {
        // Predict: integrate external forces and store previous positions.
        for (SoftBodyParticle& particle : m_particles)
        {
            particle.m_prevPosition = particle.m_position;
            if (particle.m_invMass > 0.0f)
            {
                particle.m_velocity += m_config.m_gravity * dt;
                particle.m_position += particle.m_velocity * dt;
            }
        }

        const uint32_t iterations = m_config.m_iterations > 0 ? m_config.m_iterations : 1;
        for (uint32_t iteration = 0; iteration < iterations; ++iteration)
        {
            SolveDistanceConstraints(dt);
            SolveTetVolumeConstraints(dt);
            SolveMeshVolumeConstraint(dt);
        }
        SolveGroundContacts();
        if (m_collisionSolver)
        {
            m_collisionSolver(m_particles, dt);
        }

        // Update velocities from the positional change (PBD velocity update) and damp.
        const float damping = AZStd::clamp(1.0f - m_config.m_damping * dt, 0.0f, 1.0f);
        const float invDt = 1.0f / dt;
        for (SoftBodyParticle& particle : m_particles)
        {
            if (particle.m_invMass > 0.0f)
            {
                particle.m_velocity = (particle.m_position - particle.m_prevPosition) * invDt * damping;
            }
        }
    }

    void SoftBody::SolveDistanceConstraints(float dt)
    {
        const float invDt2 = 1.0f / (dt * dt);
        for (const SoftBodyDistanceConstraint& constraint : m_distanceConstraints)
        {
            SoftBodyParticle& pa = m_particles[constraint.m_particleA];
            SoftBodyParticle& pb = m_particles[constraint.m_particleB];
            const float wSum = pa.m_invMass + pb.m_invMass;
            if (wSum <= 0.0f)
            {
                continue;
            }

            Vector3 delta = pb.m_position - pa.m_position;
            const float length = delta.GetLength();
            if (length < Epsilon)
            {
                continue;
            }
            const float constraintValue = length - constraint.m_restLength;
            const float alphaTilde = constraint.m_compliance * invDt2;
            const float lambda = -constraintValue / (wSum + alphaTilde);
            const Vector3 correction = delta * (lambda / length);

            pa.m_position -= correction * pa.m_invMass;
            pb.m_position += correction * pb.m_invMass;
        }
    }

    void SoftBody::SolveTetVolumeConstraints(float dt)
    {
        const float invDt2 = 1.0f / (dt * dt);
        for (const SoftBodyTetVolumeConstraint& constraint : m_tetVolumeConstraints)
        {
            SoftBodyParticle& p0 = m_particles[constraint.m_particles[0]];
            SoftBodyParticle& p1 = m_particles[constraint.m_particles[1]];
            SoftBodyParticle& p2 = m_particles[constraint.m_particles[2]];
            SoftBodyParticle& p3 = m_particles[constraint.m_particles[3]];

            const Vector3 grad0 = (p3.m_position - p1.m_position).Cross(p2.m_position - p1.m_position) / 6.0f;
            const Vector3 grad1 = (p2.m_position - p0.m_position).Cross(p3.m_position - p0.m_position) / 6.0f;
            const Vector3 grad2 = (p3.m_position - p0.m_position).Cross(p1.m_position - p0.m_position) / 6.0f;
            const Vector3 grad3 = (p1.m_position - p0.m_position).Cross(p2.m_position - p0.m_position) / 6.0f;

            const float wSum = p0.m_invMass * grad0.GetLengthSq() + p1.m_invMass * grad1.GetLengthSq() +
                p2.m_invMass * grad2.GetLengthSq() + p3.m_invMass * grad3.GetLengthSq();
            if (wSum <= Epsilon)
            {
                continue;
            }

            const float volume =
                SignedTetVolume(p0.m_position, p1.m_position, p2.m_position, p3.m_position);
            const float constraintValue = volume - constraint.m_restVolume;
            const float alphaTilde = constraint.m_compliance * invDt2;
            const float lambda = -constraintValue / (wSum + alphaTilde);

            p0.m_position += grad0 * (lambda * p0.m_invMass);
            p1.m_position += grad1 * (lambda * p1.m_invMass);
            p2.m_position += grad2 * (lambda * p2.m_invMass);
            p3.m_position += grad3 * (lambda * p3.m_invMass);
        }
    }

    void SoftBody::SolveMeshVolumeConstraint(float dt)
    {
        if (m_config.m_pressure <= 0.0f || m_meshIndices.size() < 3 || AZStd::abs(m_restMeshVolume) < Epsilon)
        {
            return;
        }

        // Global volume constraint (balloon/pressure model): C = V - pressure * V_rest.
        // Per-particle gradients accumulate the cross products of adjacent triangle edges.
        AZStd::vector<Vector3> gradients(m_particles.size(), Vector3::CreateZero());
        for (size_t i = 0; i + 2 < m_meshIndices.size(); i += 3)
        {
            const uint32_t ia = m_meshIndices[i];
            const uint32_t ib = m_meshIndices[i + 1];
            const uint32_t ic = m_meshIndices[i + 2];
            const Vector3& a = m_particles[ia].m_position;
            const Vector3& b = m_particles[ib].m_position;
            const Vector3& c = m_particles[ic].m_position;
            gradients[ia] += b.Cross(c) / 6.0f;
            gradients[ib] += c.Cross(a) / 6.0f;
            gradients[ic] += a.Cross(b) / 6.0f;
        }

        float wSum = 0.0f;
        for (size_t i = 0; i < m_particles.size(); ++i)
        {
            wSum += m_particles[i].m_invMass * gradients[i].GetLengthSq();
        }
        if (wSum <= Epsilon)
        {
            return;
        }

        const float constraintValue = ComputeMeshVolume() - m_config.m_pressure * m_restMeshVolume;
        const float alphaTilde = m_config.m_pressureCompliance / (dt * dt);
        const float lambda = -constraintValue / (wSum + alphaTilde);

        for (size_t i = 0; i < m_particles.size(); ++i)
        {
            SoftBodyParticle& particle = m_particles[i];
            if (particle.m_invMass > 0.0f)
            {
                particle.m_position += gradients[i] * (lambda * particle.m_invMass);
            }
        }
    }

    void SoftBody::SolveGroundContacts()
    {
        if (!m_config.m_groundPlaneEnabled)
        {
            return;
        }

        for (SoftBodyParticle& particle : m_particles)
        {
            if (particle.m_invMass <= 0.0f)
            {
                continue;
            }
            const float z = particle.m_position.GetZ();
            if (z < m_config.m_groundHeight)
            {
                particle.m_position.SetZ(m_config.m_groundHeight);

                // Approximate friction: pull the tangential motion of this substep back
                // toward where the particle came from.
                const Vector3 tangential(
                    particle.m_position.GetX() - particle.m_prevPosition.GetX(),
                    particle.m_position.GetY() - particle.m_prevPosition.GetY(),
                    0.0f);
                particle.m_position -= tangential * AZStd::clamp(m_config.m_groundFriction, 0.0f, 1.0f);
            }
        }
    }
} // namespace AZ
