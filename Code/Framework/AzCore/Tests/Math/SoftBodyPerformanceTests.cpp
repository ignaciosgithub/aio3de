/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Math/SoftBody.h>
#include <AzCore/UnitTest/TestTypes.h>

#if defined(HAVE_BENCHMARK)

#include <benchmark/benchmark.h>

namespace Benchmark
{
    using AZ::SoftBody;
    using AZ::SoftBodyConfig;
    using AZ::Vector3;

    namespace
    {
        // Regular N x N cloth grid of particles with structural + shear distance constraints,
        // top row pinned. Representative softbody workload (constraint-dominated).
        void BuildClothGrid(SoftBody& body, uint32_t gridSize)
        {
            SoftBodyConfig config;
            config.m_substeps = 4;
            config.m_iterations = 4;
            body.SetConfig(config);

            const float spacing = 0.1f;
            for (uint32_t y = 0; y < gridSize; ++y)
            {
                for (uint32_t x = 0; x < gridSize; ++x)
                {
                    const float invMass = (y == 0) ? 0.0f : 1.0f;
                    body.AddParticle(Vector3(x * spacing, 0.0f, -static_cast<float>(y) * spacing), invMass);
                }
            }
            auto index = [gridSize](uint32_t x, uint32_t y)
            {
                return y * gridSize + x;
            };
            for (uint32_t y = 0; y < gridSize; ++y)
            {
                for (uint32_t x = 0; x < gridSize; ++x)
                {
                    if (x + 1 < gridSize)
                    {
                        body.AddDistanceConstraint(index(x, y), index(x + 1, y), 0.0f);
                    }
                    if (y + 1 < gridSize)
                    {
                        body.AddDistanceConstraint(index(x, y), index(x, y + 1), 0.0f);
                    }
                    if (x + 1 < gridSize && y + 1 < gridSize)
                    {
                        body.AddDistanceConstraint(index(x, y), index(x + 1, y + 1), 0.0f);
                        body.AddDistanceConstraint(index(x + 1, y), index(x, y + 1), 0.0f);
                    }
                }
            }
        }
    } // namespace

    static void BM_SoftBodyStep(benchmark::State& state)
    {
        const uint32_t gridSize = static_cast<uint32_t>(state.range(0));
        SoftBody body;
        BuildClothGrid(body, gridSize);

        for (auto _ : state)
        {
            body.Step(1.0f / 60.0f);
            benchmark::DoNotOptimize(body.GetParticles().data());
        }

        state.SetItemsProcessed(
            static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(body.GetDistanceConstraints().size()));
        state.counters["particles"] = static_cast<double>(body.GetParticles().size());
        state.counters["constraints"] = static_cast<double>(body.GetDistanceConstraints().size());
    }
    BENCHMARK(BM_SoftBodyStep)->Arg(16)->Arg(32)->Arg(64)->Unit(benchmark::kMillisecond);
} // namespace Benchmark

#endif // HAVE_BENCHMARK
