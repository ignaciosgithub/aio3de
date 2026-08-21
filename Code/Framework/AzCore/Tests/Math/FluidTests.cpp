/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Math/Fluid.h>
#include <AzCore/UnitTest/TestTypes.h>

namespace UnitTest
{
    using FluidTests = LeakDetectionFixture;

    namespace
    {
        AZ::FluidConfig MakeTestConfig()
        {
            AZ::FluidConfig config;
            config.m_parallel = false; // Unit tests run without a TaskExecutor.
            config.m_particleSpacing = 0.1f;
            config.m_restDensity = 1000.0f;
            config.m_substeps = 2;
            config.m_iterations = 3;
            config.m_bounds = AZ::Aabb::CreateFromMinMax(AZ::Vector3(-2.0f, -2.0f, 0.0f), AZ::Vector3(2.0f, 2.0f, 4.0f));
            return config;
        }

        float SimulateAndGetMaxSpeed(AZ::FluidSim& sim, uint32_t steps, float dt)
        {
            float maxSpeed = 0.0f;
            for (uint32_t s = 0; s < steps; ++s)
            {
                sim.Step(dt);
            }
            for (const AZ::FluidParticle& p : sim.GetParticles())
            {
                maxSpeed = AZStd::max(maxSpeed, p.m_velocity.GetLength());
            }
            return maxSpeed;
        }
    } // namespace

    TEST_F(FluidTests, SpawnBox_FillsGridAtSpacing)
    {
        AZ::FluidSim sim;
        sim.SetConfig(MakeTestConfig());
        const uint32_t added = sim.SpawnBox(AZ::Vector3(0.0f, 0.0f, 0.0f), AZ::Vector3(0.3f, 0.3f, 0.3f));
        EXPECT_EQ(added, 64u); // 4^3 at 0.1 spacing over [0, 0.3]
        EXPECT_EQ(sim.GetParticles().size(), 64u);
    }

    TEST_F(FluidTests, ParticleMass_MatchesRestDensityTimesCellVolume)
    {
        AZ::FluidSim sim;
        sim.SetConfig(MakeTestConfig());
        sim.SpawnBox(AZ::Vector3::CreateZero(), AZ::Vector3(0.1f, 0.1f, 0.1f));
        // 1000 kg/m^3 * (0.1 m)^3 = 1 kg per particle.
        EXPECT_NEAR(sim.GetParticleMass(), 1.0f, 1e-4f);
        EXPECT_NEAR(sim.GetSmoothingRadius(), 0.2f, 1e-5f);
    }

    TEST_F(FluidTests, SingleParticle_FreeFallMatchesGravity)
    {
        AZ::FluidConfig config = MakeTestConfig();
        config.m_gravity = AZ::Vector3(0.0f, 0.0f, -10.0f);
        config.m_boundsEnabled = false;
        config.m_substeps = 1;
        config.m_iterations = 1;
        config.m_viscosity = 0.0f;

        AZ::FluidSim sim;
        sim.SetConfig(config);
        sim.AddParticle(AZ::Vector3(0.0f, 0.0f, 100.0f));

        const float dt = 0.01f;
        const uint32_t steps = 100; // 1 second
        for (uint32_t s = 0; s < steps; ++s)
        {
            sim.Step(dt);
        }

        // Symplectic Euler after 1s at g=10: v = -10, z = 100 - g*dt*dt*(1+2+...+n) ~ 94.95.
        const AZ::FluidParticle& p = sim.GetParticles()[0];
        EXPECT_NEAR(p.m_velocity.GetZ(), -10.0f, 0.05f);
        EXPECT_NEAR(p.m_position.GetZ(), 94.95f, 0.1f);
    }

    TEST_F(FluidTests, Bounds_ContainAllParticles)
    {
        AZ::FluidSim sim;
        sim.SetConfig(MakeTestConfig());
        sim.SpawnBox(AZ::Vector3(-0.2f, -0.2f, 1.0f), AZ::Vector3(0.2f, 0.2f, 1.4f));

        for (uint32_t s = 0; s < 120; ++s)
        {
            sim.Step(1.0f / 60.0f);
        }

        const AZ::Aabb& bounds = sim.GetConfig().m_bounds;
        for (const AZ::FluidParticle& p : sim.GetParticles())
        {
            EXPECT_TRUE(bounds.Contains(p.m_position));
        }
    }

    TEST_F(FluidTests, RestingFluid_DensityApproachesRestDensity)
    {
        AZ::FluidConfig config = MakeTestConfig();
        config.m_bounds = AZ::Aabb::CreateFromMinMax(AZ::Vector3(-0.05f, -0.05f, 0.0f), AZ::Vector3(0.45f, 0.45f, 4.0f));
        AZ::FluidSim sim;
        sim.SetConfig(config);
        sim.SpawnBox(AZ::Vector3(0.0f, 0.0f, 0.0f), AZ::Vector3(0.4f, 0.4f, 0.4f));

        for (uint32_t s = 0; s < 180; ++s)
        {
            sim.Step(1.0f / 60.0f);
        }

        // Interior density should settle near the rest density (surface particles bias it low).
        const float avgDensity = sim.ComputeAverageDensity();
        EXPECT_GT(avgDensity, 0.6f * config.m_restDensity);
        EXPECT_LT(avgDensity, 1.5f * config.m_restDensity);
    }

    TEST_F(FluidTests, HoneyDampsMotionFasterThanWater)
    {
        AZ::FluidConfig waterConfig = MakeTestConfig();
        waterConfig.m_viscosity = 0.02f;
        AZ::FluidConfig honeyConfig = MakeTestConfig();
        honeyConfig.m_viscosity = 1.0f;
        honeyConfig.m_restDensity = 1400.0f;

        auto runSplash = [](const AZ::FluidConfig& config)
        {
            AZ::FluidSim sim;
            sim.SetConfig(config);
            sim.SpawnBox(AZ::Vector3(-0.2f, -0.2f, 0.8f), AZ::Vector3(0.2f, 0.2f, 1.2f));
            for (AZ::FluidParticle& p : sim.GetParticles())
            {
                p.m_velocity = AZ::Vector3(2.0f, 0.0f, 0.0f);
            }
            // Let it splash and settle a bit, then measure remaining kinetic energy.
            float kineticEnergy = 0.0f;
            for (uint32_t s = 0; s < 60; ++s)
            {
                sim.Step(1.0f / 60.0f);
            }
            for (const AZ::FluidParticle& p : sim.GetParticles())
            {
                kineticEnergy += p.m_velocity.GetLengthSq();
            }
            return kineticEnergy / static_cast<float>(sim.GetParticles().size());
        };

        const float waterEnergy = runSplash(waterConfig);
        const float honeyEnergy = runSplash(honeyConfig);
        EXPECT_LT(honeyEnergy, waterEnergy);
    }

    TEST_F(FluidTests, Deterministic_SameInputsSameResult)
    {
        auto run = []()
        {
            AZ::FluidSim sim;
            sim.SetConfig(MakeTestConfig());
            sim.SpawnBox(AZ::Vector3(-0.2f, -0.2f, 0.5f), AZ::Vector3(0.2f, 0.2f, 0.9f));
            for (uint32_t s = 0; s < 30; ++s)
            {
                sim.Step(1.0f / 60.0f);
            }
            return sim.GetParticles();
        };

        const AZStd::vector<AZ::FluidParticle> a = run();
        const AZStd::vector<AZ::FluidParticle> b = run();
        ASSERT_EQ(a.size(), b.size());
        for (size_t i = 0; i < a.size(); ++i)
        {
            EXPECT_TRUE(a[i].m_position.IsClose(b[i].m_position, 0.0f));
            EXPECT_TRUE(a[i].m_velocity.IsClose(b[i].m_velocity, 0.0f));
        }
    }

    TEST_F(FluidTests, ExternalAcceleration_WindPushesParticles)
    {
        AZ::FluidConfig config = MakeTestConfig();
        config.m_gravity = AZ::Vector3::CreateZero();
        config.m_boundsEnabled = false;
        config.m_viscosity = 0.0f;

        AZ::FluidSim sim;
        sim.SetConfig(config);
        sim.AddParticle(AZ::Vector3::CreateZero());
        sim.SetExternalAcceleration(
            [](const AZ::Vector3&, const AZ::Vector3&)
            {
                return AZ::Vector3(4.0f, 0.0f, 0.0f);
            });

        for (uint32_t s = 0; s < 60; ++s)
        {
            sim.Step(1.0f / 60.0f);
        }

        EXPECT_NEAR(sim.GetParticles()[0].m_velocity.GetX(), 4.0f, 0.05f);
        EXPECT_GT(sim.GetParticles()[0].m_position.GetX(), 1.5f);
    }

    TEST_F(FluidTests, Fluid_RemainsStable)
    {
        AZ::FluidSim sim;
        sim.SetConfig(MakeTestConfig());
        sim.SpawnBox(AZ::Vector3(-0.3f, -0.3f, 0.5f), AZ::Vector3(0.3f, 0.3f, 1.1f));

        const float maxSpeed = SimulateAndGetMaxSpeed(sim, 300, 1.0f / 60.0f);
        EXPECT_LT(maxSpeed, 20.0f); // no explosion
        for (const AZ::FluidParticle& p : sim.GetParticles())
        {
            EXPECT_TRUE(p.m_position.IsFinite());
            EXPECT_TRUE(p.m_velocity.IsFinite());
        }
    }

    TEST_F(FluidTests, WindField_SteadyComponentDominates)
    {
        AZ::WindField::Config config;
        config.m_baseVelocity = AZ::Vector3(8.0f, 0.0f, 0.0f);
        config.m_gustStrength = 0.25f;
        config.m_turbulence = 0.1f;
        AZ::WindField wind(config);

        // Average over positions/times stays near the base velocity; instantaneous samples vary.
        AZ::Vector3 sum = AZ::Vector3::CreateZero();
        constexpr int samples = 200;
        for (int i = 0; i < samples; ++i)
        {
            const float t = 0.1f * static_cast<float>(i);
            const AZ::Vector3 pos(0.5f * static_cast<float>(i), 0.3f * static_cast<float>(i), 1.0f);
            sum += wind.Sample(pos, t);
        }
        const AZ::Vector3 average = sum / static_cast<float>(samples);
        EXPECT_NEAR(average.GetX(), 8.0f, 1.5f);
        EXPECT_NEAR(average.GetY(), 0.0f, 1.0f);
        EXPECT_NEAR(average.GetZ(), 0.0f, 0.5f);
    }

    TEST_F(FluidTests, WindField_ZeroBaseVelocityIsCalm)
    {
        AZ::WindField wind;
        AZ::WindField::Config config;
        config.m_baseVelocity = AZ::Vector3::CreateZero();
        wind.SetConfig(config);
        EXPECT_TRUE(wind.Sample(AZ::Vector3(1.0f, 2.0f, 3.0f), 5.0f).IsZero());
    }

    TEST_F(FluidTests, WindField_DragAccelerationOpposesRelativeVelocity)
    {
        AZ::WindField::Config config;
        config.m_baseVelocity = AZ::Vector3(10.0f, 0.0f, 0.0f);
        config.m_gustStrength = 0.0f;
        config.m_turbulence = 0.0f;
        AZ::WindField wind(config);

        // A body already moving with the wind feels no drag.
        const AZ::Vector3 matched = wind.DragAcceleration(AZ::Vector3::CreateZero(), AZ::Vector3(10.0f, 0.0f, 0.0f), 0.0f, 0.5f);
        EXPECT_TRUE(matched.IsClose(AZ::Vector3::CreateZero(), 1e-4f));

        // A stationary body is pushed downwind: a = (wind - v) * k = 10 * 0.5 = 5.
        const AZ::Vector3 still = wind.DragAcceleration(AZ::Vector3::CreateZero(), AZ::Vector3::CreateZero(), 0.0f, 0.5f);
        EXPECT_NEAR(still.GetX(), 5.0f, 1e-4f);
    }
} // namespace UnitTest
