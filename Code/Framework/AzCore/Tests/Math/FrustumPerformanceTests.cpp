/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Math/Frustum.h>
#include <AzCore/Math/FrustumCull.h>
#include <AzCore/Task/TaskExecutor.h>
#include <AzCore/UnitTest/TestTypes.h>

#if defined(HAVE_BENCHMARK)

#include <random>
#include <vector>
#include <benchmark/benchmark.h>

namespace Benchmark
{
    class BM_MathFrustum
        : public benchmark::Fixture
    {
        void internalSetUp()
        {
            m_testFrustum = AZ::Frustum(AZ::ViewFrustumAttributes(AZ::Transform::CreateIdentity(), 1.0f, 2.0f * atanf(0.5f), 10.0f, 90.0f));

            m_dataArray.resize(1000);

            const unsigned int seed = 1;
            std::mt19937_64 rng(seed);
            std::uniform_real_distribution<float> unif;

            std::generate(m_dataArray.begin(), m_dataArray.end(), [&unif, &rng]()
            {
                Data data;
                data.sphereCenter = AZ::Vector3(unif(rng), unif(rng), unif(rng)) * 100.0f;
                data.sphereRadius = unif(rng) * 10.0f;
                data.aabbMin = AZ::Vector3(unif(rng), unif(rng), unif(rng)) * 100.0f;
                data.aabbMax = AZ::Vector3(unif(rng), unif(rng), unif(rng)).GetAbs() * 10.0f + data.aabbMin;
                return data;
            });
        }
    public:
        void SetUp(const benchmark::State&) override
        {
            internalSetUp();
        }
        void SetUp(benchmark::State&) override
        {
            internalSetUp();
        }

        struct Data
        {
            AZ::Vector3 sphereCenter;
            float sphereRadius;
            AZ::Vector3 aabbMin;
            AZ::Vector3 aabbMax;
        };

        std::vector<Data> m_dataArray;
        AZ::Frustum m_testFrustum;
    };

    BENCHMARK_F(BM_MathFrustum, SphereIntersect)(benchmark::State& state)
    {
        for ([[maybe_unused]] auto _ : state)
        {
            for (auto& data : m_dataArray)
            {
                AZ::IntersectResult result = m_testFrustum.IntersectSphere(data.sphereCenter, data.sphereRadius);
                benchmark::DoNotOptimize(result);
            }
        }
    }

    BENCHMARK_F(BM_MathFrustum, AabbIntersect)(benchmark::State& state)
    {
        for ([[maybe_unused]] auto _ : state)
        {
            for (auto& data : m_dataArray)
            {
                AZ::IntersectResult result = m_testFrustum.IntersectAabb(data.aabbMin, data.aabbMax);
                benchmark::DoNotOptimize(result);
            }
        }
    }

    // Compares the per-sphere scalar frustum test (AZ::Frustum::IntersectSphere) against the
    // SoA + SIMD batched AZ::FrustumClassifySpheres and its multi-threaded variant
    // (AZ::FrustumClassifySpheresParallel), over the same randomized batch of bounding spheres.
    class BM_FrustumCull
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
        void TearDown(const benchmark::State&) override
        {
            teardown();
        }
        void TearDown(benchmark::State&) override
        {
            teardown();
        }

        AZ::SphereBatchSoA View() const
        {
            AZ::SphereBatchSoA soa;
            soa.m_centerX = m_centerX.data();
            soa.m_centerY = m_centerY.data();
            soa.m_centerZ = m_centerZ.data();
            soa.m_radius = m_radius.data();
            soa.m_count = m_centerX.size();
            return soa;
        }

        AZ::Frustum m_frustum{ AZ::ViewFrustumAttributes(AZ::Transform::CreateIdentity(), 1.0f, 2.0f * atanf(0.5f), 10.0f, 90.0f) };
        std::vector<float> m_centerX, m_centerY, m_centerZ, m_radius;
        std::vector<AZ::IntersectResult> m_results;
        AZ::TaskExecutor* m_executor = nullptr;

    private:
        void internalSetUp(const benchmark::State& state)
        {
            const size_t count = static_cast<size_t>(state.range(0));
            m_centerX.resize(count);
            m_centerY.resize(count);
            m_centerZ.resize(count);
            m_radius.resize(count);
            m_results.resize(count);

            std::mt19937 rng(1);
            std::uniform_real_distribution<float> coord(-150.0f, 150.0f);
            std::uniform_real_distribution<float> rad(0.1f, 8.0f);
            for (size_t i = 0; i < count; ++i)
            {
                m_centerX[i] = coord(rng);
                m_centerY[i] = coord(rng);
                m_centerZ[i] = coord(rng);
                m_radius[i] = rad(rng);
            }

            m_executor = new AZ::TaskExecutor();
            AZ::TaskExecutor::SetInstance(m_executor);
        }

        void teardown()
        {
            if (&AZ::TaskExecutor::Instance() == m_executor)
            {
                AZ::TaskExecutor::SetInstance(nullptr);
            }
            delete m_executor;
            m_executor = nullptr;
        }
    };

    BENCHMARK_DEFINE_F(BM_FrustumCull, Scalar)(benchmark::State& state)
    {
        const AZ::SphereBatchSoA spheres = View();
        for ([[maybe_unused]] auto _ : state)
        {
            for (size_t i = 0; i < spheres.m_count; ++i)
            {
                const AZ::Vector3 center(spheres.m_centerX[i], spheres.m_centerY[i], spheres.m_centerZ[i]);
                m_results[i] = m_frustum.IntersectSphere(center, spheres.m_radius[i]);
            }
            benchmark::DoNotOptimize(m_results.data());
        }
    }
    BENCHMARK_REGISTER_F(BM_FrustumCull, Scalar)->Arg(1 << 10)->Arg(1 << 14)->Arg(1 << 18)->Arg(1 << 20);

    BENCHMARK_DEFINE_F(BM_FrustumCull, BatchedSimd)(benchmark::State& state)
    {
        const AZ::SphereBatchSoA spheres = View();
        for ([[maybe_unused]] auto _ : state)
        {
            AZ::FrustumClassifySpheres(m_frustum, spheres, m_results.data());
            benchmark::DoNotOptimize(m_results.data());
        }
    }
    BENCHMARK_REGISTER_F(BM_FrustumCull, BatchedSimd)->Arg(1 << 10)->Arg(1 << 14)->Arg(1 << 18)->Arg(1 << 20);

    BENCHMARK_DEFINE_F(BM_FrustumCull, BatchedSimdParallel)(benchmark::State& state)
    {
        const AZ::SphereBatchSoA spheres = View();
        for ([[maybe_unused]] auto _ : state)
        {
            AZ::FrustumClassifySpheresParallel(m_frustum, spheres, m_results.data());
            benchmark::DoNotOptimize(m_results.data());
        }
    }
    BENCHMARK_REGISTER_F(BM_FrustumCull, BatchedSimdParallel)->Arg(1 << 10)->Arg(1 << 14)->Arg(1 << 18)->Arg(1 << 20);
}

#endif
