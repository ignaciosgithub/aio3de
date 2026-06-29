/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Math/Frustum.h>
#include <AzCore/Math/Plane.h>

namespace AZ
{
    //! Structure-of-Arrays (SoA) view over a batch of bounding spheres, laid out for
    //! cache-friendly, SIMD-batched frustum culling. Sphere centers are split into
    //! separate X/Y/Z arrays so that four spheres can be loaded into a single SIMD
    //! register and tested against a frustum plane at once.
    //!
    //! All four pointers must reference arrays of at least \ref m_count elements.
    struct SphereBatchSoA
    {
        const float* m_centerX = nullptr;
        const float* m_centerY = nullptr;
        const float* m_centerZ = nullptr;
        const float* m_radius = nullptr;
        size_t m_count = 0;
    };

    //! Classifies every sphere in \p spheres against \p frustum, writing one IntersectResult per
    //! sphere into \p results (which must have room for \p spheres.m_count entries).
    //!
    //! results[i] is exactly what AZ::Frustum::IntersectSphere would return for sphere i:
    //!   - IntersectResult::Exterior : the sphere is fully behind at least one frustum plane,
    //!   - IntersectResult::Interior : the sphere is fully inside every frustum plane,
    //!   - IntersectResult::Overlaps : the sphere straddles at least one frustum plane.
    //!
    //! Four spheres are processed per SIMD register using the same normalized plane equations and
    //! the same "distance < -radius" / "|distance| < radius" tests as Frustum::IntersectSphere, so
    //! the results match the scalar path bit-for-bit. Any remaining 0-3 spheres use the scalar path.
    void FrustumClassifySpheres(const Frustum& frustum, const SphereBatchSoA& spheres, IntersectResult* results);

    //! Identical to FrustumClassifySpheres but spreads the work across worker threads using
    //! AZ::ParallelForEachChunk (SIMD within a chunk, chunks across cores).
    //!
    //! Each chunk is contiguous, so the SoA layout and the per-chunk SIMD loop are preserved.
    //! As with all ParallelFor-based helpers, this blocks on a TaskGraphEvent and therefore must
    //! NOT be called from within a task already running on a TaskExecutor worker thread.
    //! \param minSpheresPerChunk lower bound on the spheres handed to each task; below this the
    //!        call runs inline to avoid task overhead dominating the (cheap) per-sphere work.
    void FrustumClassifySpheresParallel(
        const Frustum& frustum, const SphereBatchSoA& spheres, IntersectResult* results, size_t minSpheresPerChunk = 4096);
} // namespace AZ

#include <AzCore/Math/FrustumCull.inl>
