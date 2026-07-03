/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Math/SoftBody.h>
#include <AzCore/UnitTest/TestTypes.h>

namespace UnitTest
{
    using SoftBodyTests = LeakDetectionFixture;

    namespace
    {
        // Unit cube (8 vertices, 12 triangles, outward winding).
        void MakeUnitCube(AZStd::vector<AZ::Vector3>& positions, AZStd::vector<uint32_t>& indices)
        {
            positions = {
                AZ::Vector3(0, 0, 0), AZ::Vector3(1, 0, 0), AZ::Vector3(1, 1, 0), AZ::Vector3(0, 1, 0),
                AZ::Vector3(0, 0, 1), AZ::Vector3(1, 0, 1), AZ::Vector3(1, 1, 1), AZ::Vector3(0, 1, 1),
            };
            indices = {
                0, 2, 1, 0, 3, 2, // bottom (z=0, normal -z)
                4, 5, 6, 4, 6, 7, // top (z=1, normal +z)
                0, 1, 5, 0, 5, 4, // front (y=0)
                1, 2, 6, 1, 6, 5, // right (x=1)
                2, 3, 7, 2, 7, 6, // back (y=1)
                3, 0, 4, 3, 4, 7, // left (x=0)
            };
        }
    } // namespace

    TEST_F(SoftBodyTests, BuildFromTriangleMesh_UnitCube_CreatesParticlesEdgesAndVolume)
    {
        AZStd::vector<AZ::Vector3> positions;
        AZStd::vector<uint32_t> indices;
        MakeUnitCube(positions, indices);

        AZ::SoftBody body;
        AZ::SoftBodyConfig config;
        body.BuildFromTriangleMesh(positions, indices, 1.0f, 0.0f, config);

        EXPECT_EQ(body.GetParticles().size(), 8);
        // 12 cube edges + 6 face diagonals = 18 unique mesh edges.
        EXPECT_EQ(body.GetDistanceConstraints().size(), 18);
        EXPECT_NEAR(body.ComputeMeshVolume(), 1.0f, 1e-4f);
    }

    TEST_F(SoftBodyTests, FreeFall_MatchesNewtonianKinematics)
    {
        AZ::SoftBody body;
        AZ::SoftBodyConfig config;
        config.m_gravity = AZ::Vector3(0.0f, 0.0f, -10.0f);
        config.m_substeps = 1;
        config.m_iterations = 1;
        config.m_damping = 0.0f;
        body.SetConfig(config);
        body.AddParticle(AZ::Vector3(0.0f, 0.0f, 100.0f), 1.0f);

        // 1 second of simulation in 100 steps. Semi-implicit Euler: z = z0 - g*dt*sum(k) => ~ -g*t^2/2 - g*dt*t/2.
        const float dt = 0.01f;
        for (int i = 0; i < 100; ++i)
        {
            body.Step(dt);
        }
        const float expected = 100.0f - 0.5f * 10.0f * 1.0f * 1.0f - 0.5f * 10.0f * dt * 1.0f;
        EXPECT_NEAR(body.GetParticles()[0].m_position.GetZ(), expected, 0.05f);
        EXPECT_NEAR(body.GetParticles()[0].m_velocity.GetZ(), -10.0f, 0.05f);
    }

    TEST_F(SoftBodyTests, DistanceConstraint_RestoresRestLength)
    {
        AZ::SoftBody body;
        AZ::SoftBodyConfig config;
        config.m_gravity = AZ::Vector3::CreateZero();
        config.m_substeps = 4;
        config.m_iterations = 8;
        body.SetConfig(config);

        const uint32_t a = body.AddParticle(AZ::Vector3(0.0f, 0.0f, 0.0f), 1.0f);
        const uint32_t b = body.AddParticle(AZ::Vector3(1.0f, 0.0f, 0.0f), 1.0f);
        body.AddDistanceConstraint(a, b, 0.0f);

        // Stretch the pair, then let the constraint pull it back.
        body.GetParticles()[b].m_position = AZ::Vector3(2.0f, 0.0f, 0.0f);
        for (int i = 0; i < 20; ++i)
        {
            body.Step(1.0f / 60.0f);
        }
        const float length =
            (body.GetParticles()[b].m_position - body.GetParticles()[a].m_position).GetLength();
        EXPECT_NEAR(length, 1.0f, 1e-3f);
    }

    TEST_F(SoftBodyTests, PinnedParticle_DoesNotMove)
    {
        AZ::SoftBody body;
        AZ::SoftBodyConfig config;
        config.m_gravity = AZ::Vector3(0.0f, 0.0f, -9.81f);
        body.SetConfig(config);

        const uint32_t pinned = body.AddParticle(AZ::Vector3(0.0f, 0.0f, 1.0f), 0.0f);
        const uint32_t free = body.AddParticle(AZ::Vector3(1.0f, 0.0f, 1.0f), 1.0f);
        body.AddDistanceConstraint(pinned, free, 0.0f);

        for (int i = 0; i < 120; ++i)
        {
            body.Step(1.0f / 60.0f);
        }

        EXPECT_TRUE(body.GetParticles()[pinned].m_position.IsClose(AZ::Vector3(0.0f, 0.0f, 1.0f), 1e-5f));
        // The free particle hangs below the pin at the rest length (pendulum settles).
        const float distance =
            (body.GetParticles()[free].m_position - body.GetParticles()[pinned].m_position).GetLength();
        EXPECT_NEAR(distance, 1.0f, 5e-2f);
    }

    TEST_F(SoftBodyTests, GroundPlane_StopsFallingCube)
    {
        AZStd::vector<AZ::Vector3> positions;
        AZStd::vector<uint32_t> indices;
        MakeUnitCube(positions, indices);
        for (AZ::Vector3& p : positions)
        {
            p += AZ::Vector3(0.0f, 0.0f, 2.0f); // start 2m above the ground
        }

        AZ::SoftBody body;
        AZ::SoftBodyConfig config;
        config.m_groundPlaneEnabled = true;
        config.m_groundHeight = 0.0f;
        body.BuildFromTriangleMesh(positions, indices, 1.0f, 0.0f, config);

        for (int i = 0; i < 600; ++i) // 10 seconds
        {
            body.Step(1.0f / 60.0f);
        }

        for (const AZ::SoftBodyParticle& particle : body.GetParticles())
        {
            EXPECT_GE(particle.m_position.GetZ(), -1e-4f);
        }
        // Settled: bottom particles on the ground, cube roughly keeps its 1m height.
        EXPECT_NEAR(body.ComputeCenter().GetZ(), 0.5f, 0.1f);
    }

    TEST_F(SoftBodyTests, CollisionSolver_ExternalPlane_StopsFallingCube)
    {
        AZStd::vector<AZ::Vector3> positions;
        AZStd::vector<uint32_t> indices;
        MakeUnitCube(positions, indices);
        for (AZ::Vector3& p : positions)
        {
            p += AZ::Vector3(0.0f, 0.0f, 3.0f);
        }

        AZ::SoftBody body;
        AZ::SoftBodyConfig config; // built-in ground plane off; only the external solver collides
        body.BuildFromTriangleMesh(positions, indices, 1.0f, 0.0f, config);

        // External collision solver: a plane at z = 1 (stand-in for world collider queries).
        body.SetCollisionSolver(
            [](AZStd::vector<AZ::SoftBodyParticle>& particles, [[maybe_unused]] float dt)
            {
                for (AZ::SoftBodyParticle& particle : particles)
                {
                    if (particle.m_position.GetZ() < 1.0f)
                    {
                        particle.m_position.SetZ(1.0f);
                    }
                }
            });

        for (int i = 0; i < 600; ++i) // 10 seconds
        {
            body.Step(1.0f / 60.0f);
        }

        for (const AZ::SoftBodyParticle& particle : body.GetParticles())
        {
            EXPECT_GE(particle.m_position.GetZ(), 1.0f - 1e-4f);
        }
        EXPECT_NEAR(body.ComputeCenter().GetZ(), 1.5f, 0.1f);
    }

    TEST_F(SoftBodyTests, PressureConstraint_PreservesVolumeUnderGravity)
    {
        AZStd::vector<AZ::Vector3> positions;
        AZStd::vector<uint32_t> indices;
        MakeUnitCube(positions, indices);
        for (AZ::Vector3& p : positions)
        {
            p += AZ::Vector3(0.0f, 0.0f, 1.0f);
        }

        AZ::SoftBody body;
        AZ::SoftBodyConfig config;
        config.m_groundPlaneEnabled = true;
        config.m_groundHeight = 0.0f;
        config.m_pressure = 1.0f; // preserve rest volume
        config.m_pressureCompliance = 0.0f;
        // Soft edges so the volume constraint does the work.
        body.BuildFromTriangleMesh(positions, indices, 1.0f, 0.001f, config);

        for (int i = 0; i < 600; ++i)
        {
            body.Step(1.0f / 60.0f);
        }

        EXPECT_NEAR(body.ComputeMeshVolume(), 1.0f, 0.05f);
    }

    TEST_F(SoftBodyTests, TetVolumeConstraint_RestoresVolume)
    {
        AZ::SoftBody body;
        AZ::SoftBodyConfig config;
        config.m_gravity = AZ::Vector3::CreateZero();
        config.m_substeps = 4;
        config.m_iterations = 8;
        body.SetConfig(config);

        const uint32_t a = body.AddParticle(AZ::Vector3(0.0f, 0.0f, 0.0f), 1.0f);
        const uint32_t b = body.AddParticle(AZ::Vector3(1.0f, 0.0f, 0.0f), 1.0f);
        const uint32_t c = body.AddParticle(AZ::Vector3(0.0f, 1.0f, 0.0f), 1.0f);
        const uint32_t d = body.AddParticle(AZ::Vector3(0.0f, 0.0f, 1.0f), 1.0f);
        body.AddTetVolumeConstraint(a, b, c, d, 0.0f);

        // Squash the apex to half height; the constraint should restore the tet volume.
        body.GetParticles()[d].m_position = AZ::Vector3(0.0f, 0.0f, 0.5f);
        for (int i = 0; i < 30; ++i)
        {
            body.Step(1.0f / 60.0f);
        }

        const auto& particles = body.GetParticles();
        const float volume = (particles[b].m_position - particles[a].m_position)
                                 .Cross(particles[c].m_position - particles[a].m_position)
                                 .Dot(particles[d].m_position - particles[a].m_position) /
            6.0f;
        EXPECT_NEAR(volume, 1.0f / 6.0f, 1e-3f);
    }

    TEST_F(SoftBodyTests, Determinism_SameInputsSameResults)
    {
        auto simulate = []() -> AZ::Vector3
        {
            AZStd::vector<AZ::Vector3> positions;
            AZStd::vector<uint32_t> indices;
            MakeUnitCube(positions, indices);
            AZ::SoftBody body;
            AZ::SoftBodyConfig config;
            config.m_groundPlaneEnabled = true;
            body.BuildFromTriangleMesh(positions, indices, 1.0f, 0.0001f, config);
            for (int i = 0; i < 120; ++i)
            {
                body.Step(1.0f / 60.0f);
            }
            return body.ComputeCenter();
        };

        const AZ::Vector3 first = simulate();
        const AZ::Vector3 second = simulate();
        EXPECT_TRUE(first.IsClose(second, 0.0f));
    }
} // namespace UnitTest
