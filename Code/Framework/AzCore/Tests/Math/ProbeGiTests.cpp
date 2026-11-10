/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include <AzCore/Math/ProbeGi.h>
#include <AzCore/UnitTest/TestTypes.h>
#include <cmath>

namespace UnitTest
{
    using AZ::BvhTriangle;
    using AZ::ProbeGiEntry;
    using AZ::ProbeGridDesc;
    using AZ::RayTracingBvh;
    using AZ::ShL1Rgb;
    using AZ::Vector3;

    namespace
    {
        constexpr float Pi = 3.14159265358979323846f;

        ShL1Rgb ConstantRadianceSh(const Vector3& radiance)
        {
            ShL1Rgb sh;
            sh.m_c[0] = radiance * std::sqrt(4.0f * Pi);
            return sh;
        }

        void AppendQuad(AZStd::vector<BvhTriangle>& triangles, const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d)
        {
            triangles.push_back({ a, b, c });
            triangles.push_back({ a, c, d });
        }
    } // namespace

    TEST(ProbeGi, ConstantRadianceShRoundTrips)
    {
        constexpr uint32_t RayCount = 4096;
        const Vector3 radiance(0.7f, 1.2f, 2.4f);
        ShL1Rgb sh;
        for (uint32_t i = 0; i < RayCount; ++i)
        {
            AZ::ShAccumulate(sh, AZ::ShProjectRadiance(AZ::SphericalFibonacciDirection(i, RayCount), radiance));
        }
        AZ::ShScale(sh, 4.0f * Pi / static_cast<float>(RayCount));
        for (const Vector3& normal : { Vector3::CreateAxisX(), Vector3::CreateAxisY(), Vector3::CreateAxisZ() })
        {
            const Vector3 value = AZ::ShEvaluateIrradianceOverPi(sh, normal);
            EXPECT_NEAR(value.GetX(), radiance.GetX(), radiance.GetX() * 0.02f);
            EXPECT_NEAR(value.GetY(), radiance.GetY(), radiance.GetY() * 0.02f);
            EXPECT_NEAR(value.GetZ(), radiance.GetZ(), radiance.GetZ() * 0.02f);
        }
    }

    TEST(ProbeGi, SphericalFibonacciDirectionsAreUnitLengthAndBalanced)
    {
        constexpr uint32_t Count = 2048;
        Vector3 sum = Vector3::CreateZero();
        for (uint32_t i = 0; i < Count; ++i)
        {
            const Vector3 direction = AZ::SphericalFibonacciDirection(i, Count);
            EXPECT_NEAR(direction.GetLength(), 1.0f, 1.0e-5f);
            sum += direction;
        }
        EXPECT_LT(sum.GetLength() / static_cast<float>(Count), 0.002f);
    }

    TEST(ProbeGi, ToroidalMappingHandlesNegativeCellsAndProbeReset)
    {
        ProbeGridDesc grid;
        grid.m_origin = Vector3(-4.0f, -2.0f, -6.0f);
        grid.m_spacing = 1.0f;
        grid.m_dims[0] = 4;
        grid.m_dims[1] = 2;
        grid.m_dims[2] = 4;
        EXPECT_EQ(AZ::ProbeGridStorageIndex(grid, -1, -1, -1), 31u);
        EXPECT_EQ(AZ::ProbeGridStorageIndex(grid, -5, -3, -5), 31u);

        ProbeGiEntry probe;
        probe.m_valid = true;
        probe.m_worldCell[0] = -4;
        probe.m_worldCell[1] = -2;
        probe.m_worldCell[2] = -6;
        EXPECT_TRUE(AZ::ProbeCellMatches(probe, -4, -2, -6));
        EXPECT_FALSE(AZ::ProbeCellMatches(probe, 0, -2, -6));
    }

    TEST(ProbeGi, RecenteredProbeResetsHysteresis)
    {
        RayTracingBvh bvh;
        bvh.Build({});
        AZStd::vector<Vector3> directions;
        constexpr uint32_t RayCount = 1024;
        for (uint32_t i = 0; i < RayCount; ++i)
        {
            directions.push_back(AZ::SphericalFibonacciDirection(i, RayCount));
        }
        ProbeGiEntry probe;
        probe.m_sh = ConstantRadianceSh(Vector3(100.0f));
        probe.m_valid = true;
        probe.m_worldCell[0] = -1;
        probe.m_worldCell[1] = -1;
        probe.m_worldCell[2] = -1;
        AZ::UpdateProbe(
            bvh,
            {},
            Vector3::CreateZero(),
            0,
            0,
            0,
            Vector3::CreateZero(),
            Vector3::CreateZero(),
            directions,
            0.0f,
            0.99f,
            probe,
            [](const Vector3&) { return Vector3(2.0f); },
            {});
        const Vector3 value = AZ::ShEvaluateIrradianceOverPi(probe.m_sh, Vector3::CreateAxisZ());
        EXPECT_NEAR(value.GetX(), 2.0f, 0.05f);
        EXPECT_TRUE(AZ::ProbeCellMatches(probe, 0, 0, 0));
    }

    TEST(ProbeGi, ProbeSamplingNormalizesTrilinearWeightsAndFadesAtGridEdge)
    {
        ProbeGridDesc grid;
        grid.m_origin = Vector3(0.0f);
        grid.m_spacing = 1.0f;
        grid.m_dims[0] = grid.m_dims[1] = grid.m_dims[2] = 4;
        AZStd::vector<ProbeGiEntry> probes(64);
        const ShL1Rgb constant = ConstantRadianceSh(Vector3(0.5f, 1.0f, 2.0f));
        for (int32_t z = 0; z < 4; ++z)
        {
            for (int32_t y = 0; y < 4; ++y)
            {
                for (int32_t x = 0; x < 4; ++x)
                {
                    ProbeGiEntry& probe = probes[AZ::ProbeGridStorageIndex(grid, x, y, z)];
                    probe.m_sh = constant;
                    probe.m_worldCell[0] = x;
                    probe.m_worldCell[1] = y;
                    probe.m_worldCell[2] = z;
                    probe.m_valid = true;
                }
            }
        }
        const auto interior = AZ::SampleProbeGrid(grid, probes, Vector3(1.5f, 1.5f, 1.5f), Vector3::CreateAxisZ());
        EXPECT_NEAR(interior.second, 1.0f, 1.0e-6f);
        EXPECT_NEAR(interior.first.GetX(), 0.5f, 0.02f);
        EXPECT_NEAR(interior.first.GetY(), 1.0f, 0.02f);
        EXPECT_NEAR(interior.first.GetZ(), 2.0f, 0.04f);

        const auto edge = AZ::SampleProbeGrid(grid, probes, Vector3(0.0f, 1.5f, 1.5f), Vector3::CreateAxisZ());
        EXPECT_NEAR(edge.second, 0.0f, 1.0e-6f);
        EXPECT_EQ(AZ::SampleProbeGrid(grid, probes, Vector3(-0.1f, 1.5f, 1.5f), Vector3::CreateAxisZ()).second, 0.0f);
    }

    TEST(ProbeGi, RecenteredWrappedProbeHasNoWeightUntilUpdated)
    {
        ProbeGridDesc grid;
        grid.m_origin = Vector3(4.0f, 0.0f, 0.0f);
        grid.m_spacing = 1.0f;
        grid.m_dims[0] = grid.m_dims[1] = grid.m_dims[2] = 4;
        AZStd::vector<ProbeGiEntry> probes(64);
        const ShL1Rgb oldSh = ConstantRadianceSh(Vector3(3.0f));
        for (int32_t z = 0; z < 4; ++z)
        {
            for (int32_t y = 0; y < 4; ++y)
            {
                for (int32_t x = 0; x < 4; ++x)
                {
                    ProbeGiEntry& probe = probes[AZ::ProbeGridStorageIndex(grid, x, y, z)];
                    probe.m_sh = oldSh;
                    probe.m_worldCell[0] = x;
                    probe.m_worldCell[1] = y;
                    probe.m_worldCell[2] = z;
                    probe.m_valid = true;
                }
            }
        }

        const int32_t cellX = 5;
        const int32_t cellY = 1;
        const int32_t cellZ = 1;
        const uint32_t slot = AZ::ProbeGridStorageIndex(grid, cellX, cellY, cellZ);
        const Vector3 samplePosition(5.0f, 1.0f, 1.0f);
        EXPECT_FALSE(AZ::ProbeCellMatches(probes[slot], cellX, cellY, cellZ));
        EXPECT_EQ(AZ::SampleProbeGrid(grid, probes, samplePosition, Vector3::CreateZero()).second, 0.0f);

        RayTracingBvh bvh;
        bvh.Build({});
        AZStd::vector<Vector3> directions;
        for (uint32_t i = 0; i < 64; ++i)
        {
            directions.push_back(AZ::SphericalFibonacciDirection(i, 64));
        }
        AZ::UpdateProbe(
            bvh,
            {},
            samplePosition,
            cellX,
            cellY,
            cellZ,
            Vector3::CreateZero(),
            Vector3::CreateZero(),
            directions,
            0.0f,
            0.97f,
            probes[slot],
            [](const Vector3&) { return Vector3(0.5f); },
            {});

        EXPECT_TRUE(AZ::ProbeCellMatches(probes[slot], cellX, cellY, cellZ));
        const auto updated = AZ::SampleProbeGrid(grid, probes, samplePosition, Vector3::CreateZero());
        EXPECT_NEAR(updated.second, 1.0f, 1.0e-6f);
        EXPECT_NEAR(updated.first.GetX(), 0.5f, 0.02f);
    }

    TEST(ProbeGi, ProbeAboveBlackGroundReceivesSkyOnlyFromAbove)
    {
        AZStd::vector<BvhTriangle> triangles;
        AppendQuad(
            triangles,
            Vector3(-100.0f, -100.0f, 0.0f),
            Vector3(100.0f, -100.0f, 0.0f),
            Vector3(100.0f, 100.0f, 0.0f),
            Vector3(-100.0f, 100.0f, 0.0f));
        RayTracingBvh bvh;
        bvh.Build(triangles);

        constexpr uint32_t RayCount = 2048;
        AZStd::vector<Vector3> directions;
        directions.reserve(RayCount);
        for (uint32_t i = 0; i < RayCount; ++i)
        {
            directions.push_back(AZ::SphericalFibonacciDirection(i, RayCount));
        }
        ProbeGiEntry probe;
        const Vector3 sky(1.0f, 0.7f, 0.4f);
        AZ::UpdateProbe(
            bvh,
            { 0xffff0000u, 0xffff0000u },
            Vector3(0.0f, 0.0f, 1.0f),
            0,
            0,
            1,
            Vector3::CreateAxisZ(),
            Vector3::CreateZero(),
            directions,
            0.0f,
            0.97f,
            probe,
            [sky](const Vector3&) { return sky; },
            {});

        const Vector3 up = AZ::ShEvaluateIrradianceOverPi(probe.m_sh, Vector3::CreateAxisZ());
        const Vector3 down = AZ::ShEvaluateIrradianceOverPi(probe.m_sh, -Vector3::CreateAxisZ());
        EXPECT_NEAR(up.GetX(), sky.GetX(), 0.03f);
        EXPECT_NEAR(up.GetY(), sky.GetY(), 0.03f);
        EXPECT_NEAR(up.GetZ(), sky.GetZ(), 0.03f);
        EXPECT_NEAR(down.GetX(), 0.0f, 0.02f);
        EXPECT_NEAR(down.GetY(), 0.0f, 0.02f);
        EXPECT_NEAR(down.GetZ(), 0.0f, 0.02f);
    }

    TEST(ProbeGi, ClosedWhiteBoxWithNoSourceDecaysWithoutExceedingSky)
    {
        AZStd::vector<BvhTriangle> triangles;
        AppendQuad(triangles, Vector3(1, -1, -1), Vector3(1, -1, 1), Vector3(1, 1, 1), Vector3(1, 1, -1));
        AppendQuad(triangles, Vector3(-1, -1, -1), Vector3(-1, 1, -1), Vector3(-1, 1, 1), Vector3(-1, -1, 1));
        AppendQuad(triangles, Vector3(-1, 1, -1), Vector3(1, 1, -1), Vector3(1, 1, 1), Vector3(-1, 1, 1));
        AppendQuad(triangles, Vector3(-1, -1, -1), Vector3(-1, -1, 1), Vector3(1, -1, 1), Vector3(1, -1, -1));
        AppendQuad(triangles, Vector3(-1, -1, 1), Vector3(-1, 1, 1), Vector3(1, 1, 1), Vector3(1, -1, 1));
        AppendQuad(triangles, Vector3(-1, -1, -1), Vector3(1, -1, -1), Vector3(1, 1, -1), Vector3(-1, 1, -1));
        RayTracingBvh bvh;
        bvh.Build(triangles);

        AZStd::vector<Vector3> directions;
        constexpr uint32_t RayCount = 1024;
        for (uint32_t i = 0; i < RayCount; ++i)
        {
            directions.push_back(AZ::SphericalFibonacciDirection(i, RayCount));
        }
        ProbeGiEntry probe;
        probe.m_sh = ConstantRadianceSh(Vector3(1.0f));
        probe.m_valid = true;
        const AZStd::vector<uint32_t> albedo(12, 0xffffffffu);
        for (uint32_t iteration = 0; iteration < 50; ++iteration)
        {
            AZ::UpdateProbe(
                bvh, albedo, Vector3::CreateZero(), 0, 0, 0, Vector3::CreateAxisZ(), Vector3::CreateZero(),
                directions, 0.0f, 0.9f, probe, {}, [](const Vector3&, const Vector3&) { return Vector3::CreateZero(); });
            const Vector3 value = AZ::ShEvaluateIrradianceOverPi(probe.m_sh, Vector3::CreateAxisZ());
            EXPECT_LE(value.GetX(), 1.0f + 1.0e-4f);
        }
        EXPECT_LT(AZ::ShEvaluateIrradianceOverPi(probe.m_sh, Vector3::CreateAxisZ()).GetX(), 0.01f);
    }

    TEST(ProbeGi, BackfaceHitReturnsZero)
    {
        EXPECT_EQ(
            AZ::ShadeProbeRayHit(
                Vector3(1.0f), Vector3::CreateAxisZ(), Vector3::CreateAxisZ(), -Vector3::CreateAxisZ(),
                Vector3(10.0f), true, Vector3(1.0f)),
            Vector3::CreateZero());
    }
} // namespace UnitTest
