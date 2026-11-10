/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzCore/Math/RayTracingBvh.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/function/function_fwd.h>
#include <AzCore/std/utility/pair.h>

namespace AZ
{
    struct ShL1Rgb
    {
        Vector3 m_c[4]{};
    };

    struct ProbeGridDesc
    {
        Vector3 m_origin = Vector3::CreateZero();
        float m_spacing = 1.0f;
        uint32_t m_dims[3] = { 1, 1, 1 };
    };

    struct ProbeGiEntry
    {
        ShL1Rgb m_sh;
        int32_t m_worldCell[3] = { 0, 0, 0 };
        bool m_valid = false;
    };

    AZCORE_API ShL1Rgb ShProjectRadiance(const Vector3& direction, const Vector3& radiance);
    AZCORE_API void ShAccumulate(ShL1Rgb& target, const ShL1Rgb& value);
    AZCORE_API void ShScale(ShL1Rgb& value, float scale);
    AZCORE_API Vector3 ShEvaluateIrradianceOverPi(const ShL1Rgb& sh, const Vector3& normal);

    AZCORE_API Vector3 SphericalFibonacciDirection(uint32_t index, uint32_t count);
    AZCORE_API Vector3 SnapProbeGridOrigin(const Vector3& cameraPosition, float spacing, const uint32_t dims[3]);
    AZCORE_API uint32_t ProbeGridStorageIndex(const ProbeGridDesc& grid, int32_t cellX, int32_t cellY, int32_t cellZ);
    AZCORE_API bool ProbeCellMatches(const ProbeGiEntry& probe, int32_t cellX, int32_t cellY, int32_t cellZ);
    AZCORE_API AZStd::pair<Vector3, float> SampleProbeGrid(
        const ProbeGridDesc& grid, const AZStd::vector<ProbeGiEntry>& probes, const Vector3& position, const Vector3& normal);

    AZCORE_API Vector3 ShadeProbeRayHit(
        const Vector3& albedo,
        const Vector3& hitNormal,
        const Vector3& rayDirection,
        const Vector3& sunDirection,
        const Vector3& sunIlluminanceOverPi,
        bool sunVisible,
        const Vector3& bounceValue);

    using ProbeEnvironmentSampler = AZStd::function<Vector3(const Vector3&)>;
    using ProbeBounceSampler = AZStd::function<Vector3(const Vector3&, const Vector3&)>;

    AZCORE_API void UpdateProbe(
        const RayTracingBvh& bvh,
        const AZStd::vector<uint32_t>& primitiveAlbedoRgba8,
        const Vector3& probePosition,
        int32_t cellX,
        int32_t cellY,
        int32_t cellZ,
        const Vector3& sunDirection,
        const Vector3& sunIlluminanceOverPi,
        const AZStd::vector<Vector3>& rayDirections,
        float iblExposure,
        float hysteresis,
        ProbeGiEntry& probe,
        const ProbeEnvironmentSampler& sampleEnvironment,
        const ProbeBounceSampler& sampleBounce);
} // namespace AZ
