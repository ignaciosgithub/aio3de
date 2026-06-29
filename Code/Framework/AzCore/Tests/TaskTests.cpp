/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Task/TaskGraph.h>
#include <AzCore/Task/TaskExecutor.h>
#include <AzCore/Task/Algorithms.h>
#include <AzCore/Memory/PoolAllocator.h>

#include <AzCore/UnitTest/TestTypes.h>

#include <AzCore/std/parallel/atomic.h>
#include <cmath>
#include <random>

using AZ::TaskDescriptor;
using AZ::TaskGraph;
using AZ::TaskGraphEvent;
using AZ::TaskExecutor;
using AZ::Internal::Task;
using AZ::TaskPriority;

static TaskDescriptor defaultTD{ "TaskGraphTestTask", "TaskGraphTests" };

namespace UnitTest
{
    class TaskGraphTestFixture : public LeakDetectionFixture
    {
    public:
        void SetUp() override
        {
            LeakDetectionFixture::SetUp();

            m_executor = aznew TaskExecutor();
            TaskExecutor::SetInstance(m_executor); // SetInstance is a null-op if there is already a default instance set
        }

        void TearDown() override
        {
            if (&TaskExecutor::Instance() == m_executor) // if this test created the default instance unset it before destroying it
            {
                TaskExecutor::SetInstance(nullptr);
            }
            azdestroy(m_executor);
            LeakDetectionFixture::TearDown();
        }

    protected:
        TaskExecutor* m_executor;
    };

    TEST(TaskGraphTests, TrivialTaskLambda)
    {
        int x = 0;

        Task task(
            defaultTD,
            [&x]()
            {
                ++x;
            });
        task.Invoke();

        EXPECT_EQ(1, x);
    }

    TEST(TaskGraphTests, TrivialTaskLambdaMove)
    {
        int x = 0;

        Task task(
            defaultTD,
            [&x]()
            {
                ++x;
            });

        Task task2 = AZStd::move(task);

        task2.Invoke();

        EXPECT_EQ(1, x);
    }

    struct TrackMoves
    {
        TrackMoves() = default;

        TrackMoves(const TrackMoves&) = delete;

        TrackMoves(TrackMoves&& other)
            : moveCount{other.moveCount + 1}
        {
        }

        int moveCount = 0;
    };

    struct TrackCopies
    {
        TrackCopies() = default;

        TrackCopies(TrackCopies&&) = delete;

        TrackCopies(const TrackCopies& other)
            : copyCount{other.copyCount + 1}
        {
        }

        int copyCount = 0;
    };

    /*
    TEST(TaskGraphTests, ThisShouldNotCompile)
    {
        auto lambda = []
        {
        };

        Task task(defaultTD, lambda);
        task.Invoke();
    }
    */

    TEST(TaskGraphTests, MoveOnlyTaskLambda)
    {
        TrackMoves tm;
        int moveCount = 0;

        Task task(
            defaultTD,
            [tm = AZStd::move(tm), &moveCount]
            {
                moveCount = tm.moveCount;
            });
        task.Invoke();

        // Two moves are expected. Once into the capture body of the lambda, once to construct
        // the type erased task
        EXPECT_EQ(2, moveCount);
    }

    TEST(TaskGraphTests, MoveOnlyTaskLambdaMove)
    {
        TrackMoves tm;
        int moveCount = 0;

        Task task(
            defaultTD,
            [tm = AZStd::move(tm), &moveCount]
            {
                moveCount = tm.moveCount;
            });

        Task task2 = AZStd::move(task);
        task2.Invoke();

        EXPECT_EQ(3, moveCount);
    }

    TEST(TaskGraphTests, CopyOnlyTaskLambda)
    {
        TrackCopies tc;
        int copyCount = 0;

        Task task(
            defaultTD,
            [tc, &copyCount]
            {
                copyCount = tc.copyCount;
            });
        task.Invoke();

        // Two copies are expected. Once into the capture body of the lambda, once to construct
        // the type erased task
        EXPECT_EQ(2, copyCount);
    }

    TEST(TaskGraphTests, CopyOnlyTaskLambdaMove)
    {
        TrackCopies tc;
        int copyCount = 0;

        Task task(
            defaultTD,
            [tc, &copyCount]
            {
                copyCount = tc.copyCount;
            });
        Task task2 = AZStd::move(task);
        task2.Invoke();

        EXPECT_EQ(3, copyCount);
    }

    TEST(TaskGraphTests, DestroyLambda)
    {
        // This test ensures that for a lambda with a destructor, the destructor is invoked
        // exactly once on a non-moved-from object.
        int x = 0;
        struct TrackDestroy
        {
            TrackDestroy(int* px)
                : count{ px }
            {
            }
            TrackDestroy(TrackDestroy&& other)
                : count{ other.count }
            {
                other.count = nullptr;
            }
            ~TrackDestroy()
            {
                if (count)
                {
                    ++*count;
                }
            }
            int* count = nullptr;
        };

        {
            TrackDestroy td{ &x };
            Task task(
                defaultTD,
                [td = AZStd::move(td)]
                {
                    AZ_UNUSED(td);
                });
            task.Invoke();
            // Destructor should not have run yet (except on moved-from instances)
            EXPECT_EQ(x, 0);
        }

        // Destructor should have run now
        EXPECT_EQ(x, 1);
    }

    TEST_F(TaskGraphTestFixture, SingleTask)
    {
        AZStd::atomic_int32_t x = 0;

        TaskGraph graph{ "SingleTask" };
        graph.AddTask(
            defaultTD,
            [&x]
            {
                x = 1;
            });

        TaskGraphEvent ev{ "ev" };
        graph.SubmitOnExecutor(*m_executor, &ev);
        ev.Wait();

        EXPECT_EQ(1, x);
    }


    TEST_F(TaskGraphTestFixture, SingleTaskChain)
    {
        AZStd::atomic_int32_t x = 0;

        TaskGraph graph{ "SingleTaskChain" };
        auto a = graph.AddTask(
            defaultTD,
            [&x]
            {
                x += 1;
            });
        auto b = graph.AddTask(
            defaultTD,
            [&x]
            {
                x += 1;
            });
        b.Precedes(a);

        TaskGraphEvent ev{ "ev" };
        graph.SubmitOnExecutor(*m_executor, &ev);
        ev.Wait();

        EXPECT_EQ(2, x);
    }

    TEST_F(TaskGraphTestFixture, MultipleIndependentTaskChains)
    {
        AZStd::atomic_int32_t x = 0;
        constexpr int numChains = 5;

        TaskGraph graph{ "MultipleIndependentTaskChains" };
        for( int i = 0; i < numChains; ++i)
        {
            auto a = graph.AddTask(
                defaultTD,
                [&x]
                {
                    x += 1;
                });
            auto b = graph.AddTask(
                defaultTD,
                [&x]
                {
                    x += 1;
                });
            b.Precedes(a);
        }

        TaskGraphEvent ev{ "ev" };
        graph.SubmitOnExecutor(*m_executor, &ev);
        ev.Wait();

        EXPECT_EQ(2*numChains, x);
    }

    TEST_F(TaskGraphTestFixture, VariadicInterface)
    {
        int x = 0;

        TaskGraph graph{ "VariadicInterface" };
        auto [a, b, c] = graph.AddTasks(
            defaultTD,
            [&]
            {
                x += 3;
            },
            [&]
            {
                x = 4 * x;
            },
            [&]
            {
                x -= 1;
            });

        a.Precedes(b);
        b.Precedes(c);

        TaskGraphEvent ev{ "ev" };
        graph.SubmitOnExecutor(*m_executor, &ev);
        ev.Wait();

        EXPECT_EQ(11, x);
    }

    TEST_F(TaskGraphTestFixture, SerialGraph)
    {
        int x = 0;

        TaskGraph graph{ "SerialGraph" };
        auto a = graph.AddTask(
            defaultTD,
            [&]
            {
                x += 3;
            });
        auto b = graph.AddTask(
            defaultTD,
            [&]
            {
                x = 4 * x;
            });
        auto c = graph.AddTask(
            defaultTD,
            [&]
            {
                x -= 1;
            });

        a.Precedes(b);
        b.Precedes(c);

        TaskGraphEvent ev{ "ev" };
        graph.SubmitOnExecutor(*m_executor, &ev);
        ev.Wait();

        EXPECT_EQ(11, x);
    }

    TEST_F(TaskGraphTestFixture, DetachedGraph)
    {
        int x = 0;

        TaskGraphEvent ev{ "ev" };

        {
            TaskGraph graph{ "DetachedGraph" };
            auto a = graph.AddTask(
                defaultTD,
                [&]
                {
                    x += 3;
                });
            auto b = graph.AddTask(
                defaultTD,
                [&]
                {
                    x = 4 * x;
                });
            auto c = graph.AddTask(
                defaultTD,
                [&]
                {
                    x -= 1;
                });

            a.Precedes(b);
            b.Precedes(c);
            graph.Detach();
            graph.SubmitOnExecutor(*m_executor, &ev);
        }

        ev.Wait();

        EXPECT_EQ(11, x);
    }

    TEST_F(TaskGraphTestFixture, ForkJoin)
    {
        AZStd::atomic<int> x = 0;

        // Task a initializes x to 3
        // Task b and c toggles the lowest two bits atomically
        // Task d decrements x

        TaskGraph graph{ "ForkJoin" };
        auto a = graph.AddTask(
            defaultTD,
            [&]
            {
                x = 0b111;
            });
        auto b = graph.AddTask(
            defaultTD,
            [&]
            {
                x ^= 1;
            });
        auto c = graph.AddTask(
            defaultTD,
            [&]
            {
                x ^= 2;
            });
        auto d = graph.AddTask(
            defaultTD,
            [&]
            {
                x -= 1;
            });
        /*
             a  <-- Root
            / \
           b   c
            \ /
             d
        */
        a.Precedes(b, c);
        d.Follows(b, c);

        TaskGraphEvent ev{ "ev" };
        graph.SubmitOnExecutor(*m_executor, &ev);
        ev.Wait();

        EXPECT_EQ(3, x);
    }

    // Waiting inside a task is disallowed , test that it fails correctly
    TEST_F(TaskGraphTestFixture, SpawnSubgraph)
    {
        AZStd::atomic<int> x = 0;

        TaskGraph graph{ "SpawnSubgraph" };
        auto a = graph.AddTask(
            defaultTD,
            [&]
            {
                x = 0b111;
            });
        auto b = graph.AddTask(
            defaultTD,
            [&]
            {
                x ^= 1;
            });
        auto c = graph.AddTask(
            defaultTD,
            [&]
            {
                x ^= 2;

                TaskGraph subgraph{ "InnerSubgraph" };
                auto e = subgraph.AddTask(
                    defaultTD,
                    [&]
                    {
                        x ^= 0b1000;
                    });
                auto f = subgraph.AddTask(
                    defaultTD,
                    [&]
                    {
                        x ^= 0b10000;
                    });
                auto g = subgraph.AddTask(
                    defaultTD,
                    [&]
                    {
                        x += 0b1000;
                    });
                e.Precedes(g);
                f.Precedes(g);
                TaskGraphEvent ev{ "ev" };
                subgraph.SubmitOnExecutor(*m_executor, &ev);
                // TaskGraphEvent::Wait asserts if called on a worker thread, suppress & validate assert
                AZ_TEST_START_TRACE_SUPPRESSION;
                ev.Wait();
                AZ_TEST_STOP_TRACE_SUPPRESSION(1);
            });
        auto d = graph.AddTask(
            defaultTD,
            [&]
            {
                x -= 1;
            });
        /*
           NOTE: The ideal way to express this topology is without the wait on the subgraph
           at task g, but this is more an illustrative test. Better is to express the entire
           graph in a single larger graph.
             a  <-- Root
            / \
           b   c - f
            \   \   \
             \   e - g
              \     /
               \   /
                \ /
                 d
        */
        a.Precedes(b);
        a.Precedes(c);
        b.Precedes(d);
        c.Precedes(d);

        TaskGraphEvent ev{ "ev" };
        graph.SubmitOnExecutor(*m_executor, &ev);
        ev.Wait();
    }

    TEST_F(TaskGraphTestFixture, RetainedGraph)
    {
        AZStd::atomic<int> x = 0;

        TaskGraph graph{ "RetainedGraph" };
        auto a = graph.AddTask(
            defaultTD,
            [&]
            {
                x = 0b111;
            });
        auto b = graph.AddTask(
            defaultTD,
            [&]
            {
                x ^= 1;
            });
        auto c = graph.AddTask(
            defaultTD,
            [&]
            {
                x ^= 2;
            });
        auto d = graph.AddTask(
            defaultTD,
            [&]
            {
                x -= 1;
            });
        auto e = graph.AddTask(
            defaultTD,
            [&]
            {
                x ^= 0b1000;
            });
        auto f = graph.AddTask(
            defaultTD,
            [&]
            {
                x ^= 0b10000;
            });
        auto g = graph.AddTask(
            defaultTD,
            [&]
            {
                x += 0b1000;
            });
        /*
             a  <-- Root
            / \
           b   c - f
            \   \   \
             \   e - g
              \     /
               \   /
                \ /
                 d
        */
        a.Precedes(b, c);
        b.Precedes(d);
        c.Precedes(e, f);
        g.Follows(e, f);
        g.Precedes(d);

        TaskGraphEvent ev1{ "ev1" };
        graph.SubmitOnExecutor(*m_executor, &ev1);
        ev1.Wait();

        EXPECT_EQ(3 | 0b100000, x);
        x = 0;

        TaskGraphEvent ev2{ "ev2" };
        graph.SubmitOnExecutor(*m_executor, &ev2);
        ev2.Wait();

        EXPECT_EQ(3 | 0b100000, x);
    }

    using ParallelForTestFixture = TaskGraphTestFixture;

    TEST_F(ParallelForTestFixture, ParallelFor_VisitsEachIndexExactlyOnce)
    {
        constexpr int count = 10000;
        AZStd::vector<int> visits(count, 0);

        // Each index is owned by exactly one iteration, so writing to visits[index] is race-free.
        AZ::ParallelFor(0, count, [&visits](int index) { ++visits[index]; });

        for (int i = 0; i < count; ++i)
        {
            EXPECT_EQ(visits[i], 1) << "index " << i << " was visited " << visits[i] << " times";
        }
    }

    TEST_F(ParallelForTestFixture, ParallelFor_ConcurrentAccumulationIsCorrect)
    {
        constexpr int count = 50000;
        AZStd::atomic<int64_t> sum{ 0 };

        AZ::ParallelFor(0, count, [&sum](int index) { sum.fetch_add(index, AZStd::memory_order_relaxed); });

        const int64_t expected = (static_cast<int64_t>(count) - 1) * count / 2;
        EXPECT_EQ(sum.load(), expected);
    }

    TEST_F(ParallelForTestFixture, ParallelFor_EmptyRangeDoesNothing)
    {
        AZStd::atomic<int> calls{ 0 };
        AZ::ParallelFor(5, 5, [&calls](int) { calls.fetch_add(1); });
        AZ::ParallelFor(10, 3, [&calls](int) { calls.fetch_add(1); });
        EXPECT_EQ(calls.load(), 0);
    }

    TEST_F(ParallelForTestFixture, ParallelFor_SingleElementRange)
    {
        AZStd::atomic<int> calls{ 0 };
        int seen = -1;
        AZ::ParallelFor(7, 8, [&](int index)
            {
                seen = index;
                calls.fetch_add(1);
            });
        EXPECT_EQ(calls.load(), 1);
        EXPECT_EQ(seen, 7);
    }

    TEST_F(ParallelForTestFixture, ParallelForEachChunk_CoversFullRangeContiguouslyAndOnce)
    {
        constexpr int count = 12345;
        AZStd::vector<int> visits(count, 0);
        AZStd::atomic<int> chunkCount{ 0 };

        AZ::ParallelForEachChunk(0, count, [&](int chunkBegin, int chunkEnd)
            {
                EXPECT_LE(chunkBegin, chunkEnd);
                chunkCount.fetch_add(1);
                for (int i = chunkBegin; i < chunkEnd; ++i)
                {
                    ++visits[i];
                }
            });

        EXPECT_GE(chunkCount.load(), 1);
        for (int i = 0; i < count; ++i)
        {
            EXPECT_EQ(visits[i], 1) << "index " << i;
        }
    }

    TEST_F(ParallelForTestFixture, ParallelFor_RespectsMaxChunks)
    {
        constexpr int count = 1000;
        AZStd::atomic<int> chunkCount{ 0 };
        AZStd::vector<int> visits(count, 0);

        AZ::ParallelForEachChunk(
            0, count,
            [&](int chunkBegin, int chunkEnd)
            {
                chunkCount.fetch_add(1);
                for (int i = chunkBegin; i < chunkEnd; ++i)
                {
                    ++visits[i];
                }
            },
            AZ::DefaultParallelForDescriptor,
            /*maxChunks*/ 3);

        EXPECT_LE(chunkCount.load(), 3);
        for (int i = 0; i < count; ++i)
        {
            EXPECT_EQ(visits[i], 1);
        }
    }

    TEST_F(ParallelForTestFixture, ParallelFor_SupportsUnsignedIndexType)
    {
        constexpr size_t count = 4096;
        AZStd::atomic<uint64_t> sum{ 0 };

        AZ::ParallelFor(size_t{ 0 }, count, [&sum](size_t index) { sum.fetch_add(index, AZStd::memory_order_relaxed); });

        const uint64_t expected = (static_cast<uint64_t>(count) - 1) * count / 2;
        EXPECT_EQ(sum.load(), expected);
    }
} // namespace UnitTest

#if defined(HAVE_BENCHMARK)
namespace Benchmark
{
    class TaskGraphBenchmarkFixture : public ::benchmark::Fixture
    {
        void internalSetUp()
        {
            executor = new TaskExecutor;
            TaskExecutor::SetInstance(executor);
            graph = new TaskGraph{ "BenchmarkFixture" };
        }

        void internalTearDown()
        {
            delete graph;
            delete executor;
            TaskExecutor::SetInstance(nullptr);
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

        void TearDown(const benchmark::State&) override
        {
            internalTearDown();
        }
        void TearDown(benchmark::State&) override
        {
            internalTearDown();
        }

        TaskDescriptor descriptors[4] = { { "critical", "benchmark", TaskPriority::CRITICAL },
                                         { "high", "benchmark", TaskPriority::HIGH },
                                         { "medium", "benchmark", TaskPriority::MEDIUM },
                                         { "low", "benchmark", TaskPriority::LOW } };

        TaskGraph* graph;
        TaskExecutor* executor;
    };

    BENCHMARK_F(TaskGraphBenchmarkFixture, QueueToDequeue)(benchmark::State& state)
    {
        graph->AddTask(
            descriptors[2],
            []
            {
            });
        for ([[maybe_unused]] auto _ : state)
        {
            TaskGraphEvent ev{ "ev" };
            graph->SubmitOnExecutor(*executor, &ev);
            ev.Wait();
        }
    }

    BENCHMARK_F(TaskGraphBenchmarkFixture, OneAfterAnother)(benchmark::State& state)
    {
        auto a = graph->AddTask(
            descriptors[2],
            []
            {
            });
        auto b = graph->AddTask(
            descriptors[2],
            []
            {
            });
        a.Precedes(b);

        for ([[maybe_unused]] auto _ : state)
        {
            TaskGraphEvent ev{ "ev" };
            graph->SubmitOnExecutor(*executor, &ev);
            ev.Wait();
        }
    }

    BENCHMARK_F(TaskGraphBenchmarkFixture, FourToOneJoin)(benchmark::State& state)
    {
        auto [a, b, c, d, e] = graph->AddTasks(
            descriptors[2],
            []
            {
            },
            []
            {
            },
            []
            {
            },
            []
            {
            },
            []
            {
            });

        e.Follows(a, b, c, d);

        for ([[maybe_unused]] auto _ : state)
        {
            TaskGraphEvent ev{ "ev" };
            graph->SubmitOnExecutor(*executor, &ev);
            ev.Wait();
        }
    }

    // Representative data-oriented (SoA) workload: integrate a large array of particle transforms,
    // then renormalize the resulting orientation vector. This mirrors the kind of per-frame,
    // high-count component update B-4 targets and is used to compare a serial loop against
    // AZ::ParallelFor across worker threads.
    class ParallelForBenchmarkFixture : public ::benchmark::Fixture
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
            internalTearDown();
        }
        void TearDown(benchmark::State&) override
        {
            internalTearDown();
        }

        void IntegrateRange(size_t begin, size_t end)
        {
            constexpr float dt = 1.0f / 60.0f;
            for (size_t i = begin; i < end; ++i)
            {
                m_posX[i] += m_velX[i] * dt;
                m_posY[i] += m_velY[i] * dt;
                m_posZ[i] += m_velZ[i] * dt;

                float length = std::sqrt(m_posX[i] * m_posX[i] + m_posY[i] * m_posY[i] + m_posZ[i] * m_posZ[i]) + 1e-6f;
                const float invLength = 1.0f / length;
                m_dirX[i] = m_posX[i] * invLength;
                m_dirY[i] = m_posY[i] * invLength;
                m_dirZ[i] = m_posZ[i] * invLength;
            }
        }

    protected:
        // Raw arrays managed entirely within SetUp/TearDown (mirroring TaskGraphBenchmarkFixture):
        // this keeps every SystemAllocator-backed object's lifetime strictly inside the benchmark
        // run, avoiding allocator-environment teardown ordering issues.
        void internalSetUp(const benchmark::State& state)
        {
            m_executor = new TaskExecutor;
            TaskExecutor::SetInstance(m_executor);

            m_count = static_cast<size_t>(state.range(0));
            m_posX = new float[m_count];
            m_posY = new float[m_count];
            m_posZ = new float[m_count];
            m_velX = new float[m_count];
            m_velY = new float[m_count];
            m_velZ = new float[m_count];
            m_dirX = new float[m_count];
            m_dirY = new float[m_count];
            m_dirZ = new float[m_count];
            for (size_t i = 0; i < m_count; ++i)
            {
                m_posX[i] = static_cast<float>(i % 13);
                m_posY[i] = static_cast<float>(i % 7) + 1.0f;
                m_posZ[i] = static_cast<float>(i % 5);
                m_velX[i] = 1.0f;
                m_velY[i] = -0.5f;
                m_velZ[i] = 0.25f;
                m_dirX[i] = 0.0f;
                m_dirY[i] = 0.0f;
                m_dirZ[i] = 0.0f;
            }
        }

        void internalTearDown()
        {
            delete[] m_posX;
            delete[] m_posY;
            delete[] m_posZ;
            delete[] m_velX;
            delete[] m_velY;
            delete[] m_velZ;
            delete[] m_dirX;
            delete[] m_dirY;
            delete[] m_dirZ;
            delete m_executor;
            TaskExecutor::SetInstance(nullptr);
        }

        TaskExecutor* m_executor = nullptr;
        size_t m_count = 0;
        float* m_posX = nullptr;
        float* m_posY = nullptr;
        float* m_posZ = nullptr;
        float* m_velX = nullptr;
        float* m_velY = nullptr;
        float* m_velZ = nullptr;
        float* m_dirX = nullptr;
        float* m_dirY = nullptr;
        float* m_dirZ = nullptr;
    };

    BENCHMARK_DEFINE_F(ParallelForBenchmarkFixture, IntegrateSerial)(benchmark::State& state)
    {
        const size_t count = static_cast<size_t>(state.range(0));
        for ([[maybe_unused]] auto _ : state)
        {
            IntegrateRange(0, count);
            benchmark::DoNotOptimize(m_dirX);
        }
    }
    BENCHMARK_REGISTER_F(ParallelForBenchmarkFixture, IntegrateSerial)->Arg(1 << 12)->Arg(1 << 16)->Arg(1 << 20);

    BENCHMARK_DEFINE_F(ParallelForBenchmarkFixture, IntegrateParallel)(benchmark::State& state)
    {
        const size_t count = static_cast<size_t>(state.range(0));
        for ([[maybe_unused]] auto _ : state)
        {
            AZ::ParallelForEachChunk(size_t{ 0 }, count, [this](size_t begin, size_t end) { IntegrateRange(begin, end); });
            benchmark::DoNotOptimize(m_dirX);
        }
    }
    BENCHMARK_REGISTER_F(ParallelForBenchmarkFixture, IntegrateParallel)->Arg(1 << 12)->Arg(1 << 16)->Arg(1 << 20);
} // namespace Benchmark
#endif
