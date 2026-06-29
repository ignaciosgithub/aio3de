/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Task/TaskGraph.h>
#include <AzCore/Task/TaskDescriptor.h>

namespace AZ
{
    //! TaskGraph-based parallel iteration helpers.
    //!
    //! AzCore historically only provided a Job-based parallel-for (AzCore/Jobs/Algorithms.h).
    //! The engine is incrementally migrating from the Job system to the TaskGraph/TaskExecutor
    //! system (see Gems/Atom MeshFeatureProcessor, which builds chunked task graphs by hand).
    //! These helpers provide that same chunked parallel-for pattern as a single reusable
    //! primitive built directly on TaskGraph, so callers no longer have to re-implement range
    //! partitioning and submission boilerplate.
    //!
    //! The range [begin, end) is split into (at most) @p maxChunks contiguous sub-ranges and each
    //! sub-range is submitted as an independent task. The call blocks until every chunk completes.
    //!
    //! Threading contract:
    //!  - The supplied kernel is invoked concurrently from multiple worker threads, so it must be
    //!    safe to call in parallel (operate only on its own sub-range / index, or synchronize any
    //!    shared writes).
    //!  - Because the call blocks on a waitable TaskGraph, it MUST NOT be invoked from within a
    //!    task that is already running on a TaskExecutor worker thread (a waitable graph cannot be
    //!    submitted from an active task thread). Call it from the main/simulation thread instead.
    //!  - Small ranges (or a resolved chunk count of 1) run inline on the calling thread to avoid
    //!    task scheduling overhead.

    inline constexpr TaskDescriptor DefaultParallelForDescriptor{ "ParallelFor", "ParallelFor" };

    //! Invokes @p kernel(chunkBegin, chunkEnd) once per chunk, where [chunkBegin, chunkEnd) is a
    //! contiguous sub-range of [begin, end). Prefer this overload when the kernel can amortize
    //! work across a whole sub-range.
    //! @param maxChunks Upper bound on the number of chunks/tasks created. 0 means use the
    //!        hardware thread count.
    template<typename IndexType, typename Kernel>
    void ParallelForEachChunk(
        IndexType begin,
        IndexType end,
        Kernel&& kernel,
        const TaskDescriptor& descriptor = DefaultParallelForDescriptor,
        uint32_t maxChunks = 0);

    //! Invokes @p kernel(index) once per index in [begin, end), distributing indices across worker
    //! threads in contiguous chunks.
    //! @param maxChunks Upper bound on the number of chunks/tasks created. 0 means use the
    //!        hardware thread count.
    template<typename IndexType, typename Kernel>
    void ParallelFor(
        IndexType begin,
        IndexType end,
        Kernel&& kernel,
        const TaskDescriptor& descriptor = DefaultParallelForDescriptor,
        uint32_t maxChunks = 0);
} // namespace AZ

#include <AzCore/Task/Algorithms.inl>
