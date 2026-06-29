/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Math/SimdMath.h>
#include <AzCore/Task/Algorithms.h>
#include <AzCore/std/algorithm.h>

namespace AZ
{
    AZ_MATH_INLINE void FrustumClassifySpheres(const Frustum& frustum, const SphereBatchSoA& spheres, IntersectResult* results)
    {
        using Vec4 = AZ::Simd::Vec4;

        // Pre-broadcast each of the six normalized plane equations (nx, ny, nz, d) so the inner
        // loop only does multiply-adds. These match the planes used by Frustum::IntersectSphere.
        Vec4::FloatType planeNx[Frustum::PlaneId::MAX];
        Vec4::FloatType planeNy[Frustum::PlaneId::MAX];
        Vec4::FloatType planeNz[Frustum::PlaneId::MAX];
        Vec4::FloatType planeD[Frustum::PlaneId::MAX];
        for (int planeId = 0; planeId < Frustum::PlaneId::MAX; ++planeId)
        {
            const Plane plane = frustum.GetPlane(static_cast<Frustum::PlaneId>(planeId));
            const Vector3 normal = plane.GetNormal();
            planeNx[planeId] = Vec4::Splat(normal.GetX());
            planeNy[planeId] = Vec4::Splat(normal.GetY());
            planeNz[planeId] = Vec4::Splat(normal.GetZ());
            planeD[planeId] = Vec4::Splat(plane.GetDistance());
        }

        const size_t count = spheres.m_count;
        const size_t simdCount = count & ~size_t(3); // round down to a multiple of 4
        const Vec4::FloatType zero = Vec4::ZeroFloat();

        // IntersectResult codes as floats: Interior == 0, Overlaps == 1, Exterior == 2.
        const Vec4::FloatType overlapsCode = Vec4::Splat(static_cast<float>(IntersectResult::Overlaps));
        const Vec4::FloatType exteriorCode = Vec4::Splat(static_cast<float>(IntersectResult::Exterior));

        size_t i = 0;
        for (; i < simdCount; i += 4)
        {
            const Vec4::FloatType cx = Vec4::LoadUnaligned(spheres.m_centerX + i);
            const Vec4::FloatType cy = Vec4::LoadUnaligned(spheres.m_centerY + i);
            const Vec4::FloatType cz = Vec4::LoadUnaligned(spheres.m_centerZ + i);
            const Vec4::FloatType radius = Vec4::LoadUnaligned(spheres.m_radius + i);
            const Vec4::FloatType negRadius = Vec4::Sub(zero, radius);

            // For each plane accumulate two masks, mirroring Frustum::IntersectSphere:
            //   exterior |= (distance < -radius)        -> fully behind a plane
            //   straddle |= (|distance| < radius)       -> within the plane's radius band
            Vec4::FloatType exteriorMask = zero;
            Vec4::FloatType straddleMask = zero;
            for (int planeId = 0; planeId < Frustum::PlaneId::MAX; ++planeId)
            {
                // distance = nx*cx + ny*cy + nz*cz + d
                Vec4::FloatType distance = planeD[planeId];
                distance = Vec4::Madd(planeNx[planeId], cx, distance);
                distance = Vec4::Madd(planeNy[planeId], cy, distance);
                distance = Vec4::Madd(planeNz[planeId], cz, distance);

                exteriorMask = Vec4::Or(exteriorMask, Vec4::CmpLt(distance, negRadius));
                straddleMask = Vec4::Or(straddleMask, Vec4::CmpLt(Vec4::Abs(distance), radius));
            }

            // code = exterior ? Exterior : (straddle ? Overlaps : Interior).
            // Exterior takes precedence, matching IntersectSphere's early-out on the first plane behind.
            Vec4::FloatType code = Vec4::Select(overlapsCode, zero, straddleMask);
            code = Vec4::Select(exteriorCode, code, exteriorMask);

            float decoded[4];
            Vec4::StoreUnaligned(decoded, code);
            results[i + 0] = static_cast<IntersectResult>(static_cast<int>(decoded[0] + 0.5f));
            results[i + 1] = static_cast<IntersectResult>(static_cast<int>(decoded[1] + 0.5f));
            results[i + 2] = static_cast<IntersectResult>(static_cast<int>(decoded[2] + 0.5f));
            results[i + 3] = static_cast<IntersectResult>(static_cast<int>(decoded[3] + 0.5f));
        }

        // Tail: handle the remaining 0-3 spheres with the canonical scalar path so results match exactly.
        for (; i < count; ++i)
        {
            const Vector3 center(spheres.m_centerX[i], spheres.m_centerY[i], spheres.m_centerZ[i]);
            results[i] = frustum.IntersectSphere(center, spheres.m_radius[i]);
        }
    }

    AZ_MATH_INLINE void FrustumClassifySpheresParallel(
        const Frustum& frustum, const SphereBatchSoA& spheres, IntersectResult* results, size_t minSpheresPerChunk)
    {
        const size_t count = spheres.m_count;
        if (count < minSpheresPerChunk || minSpheresPerChunk == 0)
        {
            FrustumClassifySpheres(frustum, spheres, results);
            return;
        }

        const uint32_t maxChunks = static_cast<uint32_t>(AZStd::max<size_t>(size_t(1), count / minSpheresPerChunk));
        static const TaskDescriptor descriptor{ "FrustumClassifySpheres", "Graphics" };
        ParallelForEachChunk(
            size_t(0),
            count,
            [&frustum, &spheres, results](size_t chunkBegin, size_t chunkEnd)
            {
                SphereBatchSoA subBatch;
                subBatch.m_centerX = spheres.m_centerX + chunkBegin;
                subBatch.m_centerY = spheres.m_centerY + chunkBegin;
                subBatch.m_centerZ = spheres.m_centerZ + chunkBegin;
                subBatch.m_radius = spheres.m_radius + chunkBegin;
                subBatch.m_count = chunkEnd - chunkBegin;
                FrustumClassifySpheres(frustum, subBatch, results + chunkBegin);
            },
            descriptor,
            maxChunks);
    }
} // namespace AZ
