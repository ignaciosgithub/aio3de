/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Task/TaskExecutor.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/parallel/thread.h>
#include <AzCore/std/utils.h>

namespace AZ
{
    template<typename IndexType, typename Kernel>
    void ParallelForEachChunk(
        IndexType begin, IndexType end, Kernel&& kernel, const TaskDescriptor& descriptor, uint32_t maxChunks)
    {
        static_assert(AZStd::is_integral_v<IndexType>, "ParallelForEachChunk requires an integral index type");

        if (begin >= end)
        {
            return;
        }

        const IndexType total = static_cast<IndexType>(end - begin);

        uint32_t hardwareThreads = AZStd::thread::hardware_concurrency();
        if (hardwareThreads == 0)
        {
            hardwareThreads = 1;
        }

        uint32_t chunkCount = (maxChunks == 0) ? hardwareThreads : maxChunks;
        chunkCount = AZStd::min(chunkCount, aznumeric_cast<uint32_t>(total));

        // For trivial ranges, skip the task machinery entirely and run inline on the calling thread.
        // This also keeps the helper usable in single-threaded contexts.
        if (chunkCount <= 1)
        {
            kernel(begin, end);
            return;
        }

        const IndexType baseChunkSize = static_cast<IndexType>(total / chunkCount);
        const IndexType remainder = static_cast<IndexType>(total % chunkCount);

        TaskGraph taskGraph{ descriptor.taskName ? descriptor.taskName : "ParallelFor" };
        TaskGraphEvent finishedEvent{ "ParallelFor Wait" };

        IndexType cursor = begin;
        for (uint32_t chunk = 0; chunk < chunkCount; ++chunk)
        {
            // Distribute the remainder across the first chunks so chunk sizes differ by at most one.
            const IndexType chunkSize = static_cast<IndexType>(baseChunkSize + (chunk < remainder ? IndexType{ 1 } : IndexType{ 0 }));
            const IndexType chunkBegin = cursor;
            const IndexType chunkEnd = static_cast<IndexType>(cursor + chunkSize);
            cursor = chunkEnd;

            // kernel is captured by reference: it outlives the graph because we block on finishedEvent below.
            taskGraph.AddTask(
                descriptor,
                [chunkBegin, chunkEnd, &kernel]()
                {
                    kernel(chunkBegin, chunkEnd);
                });
        }

        taskGraph.Submit(&finishedEvent);
        finishedEvent.Wait();
    }

    template<typename IndexType, typename Kernel>
    void ParallelFor(IndexType begin, IndexType end, Kernel&& kernel, const TaskDescriptor& descriptor, uint32_t maxChunks)
    {
        ParallelForEachChunk(
            begin,
            end,
            [&kernel](IndexType chunkBegin, IndexType chunkEnd)
            {
                for (IndexType index = chunkBegin; index < chunkEnd; ++index)
                {
                    kernel(index);
                }
            },
            descriptor,
            maxChunks);
    }
} // namespace AZ
