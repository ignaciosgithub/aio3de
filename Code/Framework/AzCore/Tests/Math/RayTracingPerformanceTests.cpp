/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Math/RayTracedShadows.h>
#include <AzCore/Math/RayTracingBvh.h>
#include <AzCore/UnitTest/TestTypes.h>

#if defined(HAVE_BENCHMARK)

#include <random>
#include <vector>
#include <benchmark/benchmark.h>

namespace Benchmark
{
    using AZ::BvhTriangle;
    using AZ::RayTracingBvh;
    using AZ::Vector3;

    // Random triangle-soup scene of small triangles scattered in a cube, plus a batch of rays
    // fired from random points toward random directions. Exercises BVH build, closest-hit and
    // any-hit traversal, and the directional-shadow batch helper at several scene sizes.
    class BM_RayTracingBvh
        : public benchmark::Fixture
    {
    public:
        void SetUp(const benchmark::State& state) override
        {
            internalSetUp(state);
        }
        void SetUp(benchmark::State& state) override
        {
            internalSetUp(state);
        }

        static constexpr size_t RayCount = 4096;
        static constexpr float SceneExtent = 100.0f;

        AZStd::vector<BvhTriangle> m_triangles;
        RayTracingBvh m_bvh;
        std::vector<Vector3> m_origins;
        std::vector<Vector3> m_directions;

    private:
        void internalSetUp(const benchmark::State& state)
        {
            const size_t triangleCount = static_cast<size_t>(state.range(0));
            std::mt19937 rng(7u);
            std::uniform_real_distribution<float> coord(-SceneExtent, SceneExtent);
            std::uniform_real_distribution<float> edge(0.2f, 2.0f);
            std::uniform_real_distribution<float> unit(-1.0f, 1.0f);

            m_triangles.clear();
            m_triangles.reserve(triangleCount);
            for (size_t i = 0; i < triangleCount; ++i)
            {
                const Vector3 a(coord(rng), coord(rng), coord(rng));
                const Vector3 b = a + Vector3(edge(rng), edge(rng), 0.0f);
                const Vector3 c = a + Vector3(0.0f, edge(rng), edge(rng));
                m_triangles.push_back({ a, b, c });
            }
            m_bvh.Build(m_triangles);

            m_origins.resize(RayCount);
            m_directions.resize(RayCount);
            for (size_t i = 0; i < RayCount; ++i)
            {
                m_origins[i] = Vector3(coord(rng), coord(rng), coord(rng));
                m_directions[i] = Vector3(unit(rng), unit(rng), unit(rng)).GetNormalizedSafe();
            }
        }
    };

    BENCHMARK_DEFINE_F(BM_RayTracingBvh, Build)(benchmark::State& state)
    {
        for ([[maybe_unused]] auto _ : state)
        {
            RayTracingBvh bvh;
            bvh.Build(m_triangles);
            benchmark::DoNotOptimize(bvh.GetNodeCount());
        }
        state.SetItemsProcessed(state.iterations() * state.range(0));
    }
    BENCHMARK_REGISTER_F(BM_RayTracingBvh, Build)->Arg(1 << 10)->Arg(1 << 14)->Arg(1 << 17);

    BENCHMARK_DEFINE_F(BM_RayTracingBvh, IntersectClosest)(benchmark::State& state)
    {
        for ([[maybe_unused]] auto _ : state)
        {
            for (size_t i = 0; i < RayCount; ++i)
            {
                AZ::BvhRayHit hit = m_bvh.IntersectClosest(m_origins[i], m_directions[i], 4.0f * SceneExtent);
                benchmark::DoNotOptimize(hit);
            }
        }
        state.SetItemsProcessed(state.iterations() * RayCount);
    }
    BENCHMARK_REGISTER_F(BM_RayTracingBvh, IntersectClosest)->Arg(1 << 10)->Arg(1 << 14)->Arg(1 << 17);

    BENCHMARK_DEFINE_F(BM_RayTracingBvh, IntersectAny)(benchmark::State& state)
    {
        for ([[maybe_unused]] auto _ : state)
        {
            for (size_t i = 0; i < RayCount; ++i)
            {
                bool hit = m_bvh.IntersectAny(m_origins[i], m_directions[i], 4.0f * SceneExtent);
                benchmark::DoNotOptimize(hit);
            }
        }
        state.SetItemsProcessed(state.iterations() * RayCount);
    }
    BENCHMARK_REGISTER_F(BM_RayTracingBvh, IntersectAny)->Arg(1 << 10)->Arg(1 << 14)->Arg(1 << 17);

    BENCHMARK_DEFINE_F(BM_RayTracingBvh, DirectionalShadowBatch)(benchmark::State& state)
    {
        AZStd::vector<AZ::ShadowSample> samples(RayCount);
        for (size_t i = 0; i < RayCount; ++i)
        {
            samples[i] = { m_origins[i], Vector3(0.0f, 0.0f, 1.0f) };
        }
        AZ::ShadowRayParams params;
        params.m_toLight = Vector3(0.3f, 0.2f, 1.0f).GetNormalizedSafe();
        params.m_maxDistance = 4.0f * SceneExtent;

        AZStd::vector<float> visibility;
        for ([[maybe_unused]] auto _ : state)
        {
            AZ::ComputeDirectionalShadowVisibility(m_bvh, samples, params, visibility);
            benchmark::DoNotOptimize(visibility.data());
        }
        state.SetItemsProcessed(state.iterations() * RayCount);
    }
    BENCHMARK_REGISTER_F(BM_RayTracingBvh, DirectionalShadowBatch)->Arg(1 << 10)->Arg(1 << 14)->Arg(1 << 17);
}

#endif
