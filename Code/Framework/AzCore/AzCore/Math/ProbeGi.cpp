/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include <AzCore/Math/ProbeGi.h>

#include <AzCore/std/algorithm.h>
#include <AzCore/std/function/function_template.h>
#include <cmath>

namespace AZ
{
    namespace
    {
        constexpr float Pi = 3.14159265358979323846f;
        constexpr float ShY00 = 0.28209479177387814f;
        constexpr float ShY1 = 0.4886025119029199f;

        int32_t PositiveModulo(int32_t value, uint32_t divisor)
        {
            const int32_t signedDivisor = static_cast<int32_t>(AZStd::max(1u, divisor));
            return ((value % signedDivisor) + signedDivisor) % signedDivisor;
        }

        uint32_t GridSize(const ProbeGridDesc& grid)
        {
            return grid.m_dims[0] * grid.m_dims[1] * grid.m_dims[2];
        }

        Vector3 DecodeAlbedo(uint32_t rgba)
        {
            constexpr float Inv255 = 1.0f / 255.0f;
            return Vector3(
                static_cast<float>(rgba & 0xffu) * Inv255,
                static_cast<float>((rgba >> 8) & 0xffu) * Inv255,
                static_cast<float>((rgba >> 16) & 0xffu) * Inv255);
        }
    } // namespace

    ShL1Rgb ShProjectRadiance(const Vector3& direction, const Vector3& radiance)
    {
        const Vector3 dir = direction.GetNormalizedSafe();
        ShL1Rgb result;
        result.m_c[0] = radiance * ShY00;
        result.m_c[1] = radiance * (ShY1 * dir.GetX());
        result.m_c[2] = radiance * (ShY1 * dir.GetY());
        result.m_c[3] = radiance * (ShY1 * dir.GetZ());
        return result;
    }

    void ShAccumulate(ShL1Rgb& target, const ShL1Rgb& value)
    {
        for (uint32_t i = 0; i < 4; ++i)
        {
            target.m_c[i] += value.m_c[i];
        }
    }

    void ShScale(ShL1Rgb& value, float scale)
    {
        for (Vector3& coefficient : value.m_c)
        {
            coefficient *= scale;
        }
    }

    Vector3 ShEvaluateIrradianceOverPi(const ShL1Rgb& sh, const Vector3& normal)
    {
        const Vector3 n = normal.GetNormalizedSafe();
        Vector3 result = sh.m_c[0] * ShY00;
        result += sh.m_c[1] * (ShY1 * n.GetX() * (2.0f / 3.0f));
        result += sh.m_c[2] * (ShY1 * n.GetY() * (2.0f / 3.0f));
        result += sh.m_c[3] * (ShY1 * n.GetZ() * (2.0f / 3.0f));
        return Vector3(AZStd::max(0.0f, result.GetX()), AZStd::max(0.0f, result.GetY()), AZStd::max(0.0f, result.GetZ()));
    }

    Vector3 SphericalFibonacciDirection(uint32_t index, uint32_t count)
    {
        if (count == 0)
        {
            return Vector3::CreateAxisZ();
        }
        const float z = 1.0f - 2.0f * (static_cast<float>(index) + 0.5f) / static_cast<float>(count);
        const float radius = std::sqrt(AZStd::max(0.0f, 1.0f - z * z));
        constexpr float GoldenAngle = 2.39996322972865332f;
        const float phi = GoldenAngle * static_cast<float>(index);
        return Vector3(radius * std::cos(phi), radius * std::sin(phi), z);
    }

    Vector3 SnapProbeGridOrigin(const Vector3& cameraPosition, float spacing, const uint32_t dims[3])
    {
        const float safeSpacing = AZStd::max(spacing, 1.0e-4f);
        return Vector3(
            (std::floor(cameraPosition.GetX() / safeSpacing) - static_cast<float>(dims[0]) * 0.5f) * safeSpacing,
            (std::floor(cameraPosition.GetY() / safeSpacing) - static_cast<float>(dims[1]) * 0.5f) * safeSpacing,
            (std::floor(cameraPosition.GetZ() / safeSpacing) - static_cast<float>(dims[2]) * 0.5f) * safeSpacing);
    }

    uint32_t ProbeGridStorageIndex(const ProbeGridDesc& grid, int32_t cellX, int32_t cellY, int32_t cellZ)
    {
        const uint32_t x = static_cast<uint32_t>(PositiveModulo(cellX, grid.m_dims[0]));
        const uint32_t y = static_cast<uint32_t>(PositiveModulo(cellY, grid.m_dims[1]));
        const uint32_t z = static_cast<uint32_t>(PositiveModulo(cellZ, grid.m_dims[2]));
        return x + grid.m_dims[0] * (y + grid.m_dims[1] * z);
    }

    bool ProbeCellMatches(const ProbeGiEntry& probe, int32_t cellX, int32_t cellY, int32_t cellZ)
    {
        return probe.m_valid && probe.m_worldCell[0] == cellX && probe.m_worldCell[1] == cellY && probe.m_worldCell[2] == cellZ;
    }

    AZStd::pair<Vector3, float> SampleProbeGrid(
        const ProbeGridDesc& grid, const AZStd::vector<ProbeGiEntry>& probes, const Vector3& position, const Vector3& normal)
    {
        if (grid.m_spacing <= 0.0f || grid.m_dims[0] == 0 || grid.m_dims[1] == 0 || grid.m_dims[2] == 0 ||
            probes.size() < GridSize(grid))
        {
            return { Vector3::CreateZero(), 0.0f };
        }

        const Vector3 n = normal.GetNormalizedSafe();
        const Vector3 biasedPosition = position + n * (0.2f * grid.m_spacing);
        const Vector3 coordinate(
            (biasedPosition.GetX() - grid.m_origin.GetX()) / grid.m_spacing,
            (biasedPosition.GetY() - grid.m_origin.GetY()) / grid.m_spacing,
            (biasedPosition.GetZ() - grid.m_origin.GetZ()) / grid.m_spacing);
        const Vector3 rawCoordinate(
            (position.GetX() - grid.m_origin.GetX()) / grid.m_spacing,
            (position.GetY() - grid.m_origin.GetY()) / grid.m_spacing,
            (position.GetZ() - grid.m_origin.GetZ()) / grid.m_spacing);

        if (rawCoordinate.GetX() < 0.0f || rawCoordinate.GetY() < 0.0f || rawCoordinate.GetZ() < 0.0f ||
            rawCoordinate.GetX() > static_cast<float>(grid.m_dims[0] - 1) ||
            rawCoordinate.GetY() > static_cast<float>(grid.m_dims[1] - 1) ||
            rawCoordinate.GetZ() > static_cast<float>(grid.m_dims[2] - 1))
        {
            return { Vector3::CreateZero(), 0.0f };
        }

        const int32_t base[3] = {
            AZStd::clamp(static_cast<int32_t>(std::floor(coordinate.GetX())), 0, static_cast<int32_t>(grid.m_dims[0]) - 1),
            AZStd::clamp(static_cast<int32_t>(std::floor(coordinate.GetY())), 0, static_cast<int32_t>(grid.m_dims[1]) - 1),
            AZStd::clamp(static_cast<int32_t>(std::floor(coordinate.GetZ())), 0, static_cast<int32_t>(grid.m_dims[2]) - 1)
        };
        const float frac[3] = {
            AZStd::clamp(coordinate.GetX() - std::floor(coordinate.GetX()), 0.0f, 1.0f),
            AZStd::clamp(coordinate.GetY() - std::floor(coordinate.GetY()), 0.0f, 1.0f),
            AZStd::clamp(coordinate.GetZ() - std::floor(coordinate.GetZ()), 0.0f, 1.0f)
        };

        float edgeDistance = AZStd::min(
            AZStd::min(AZStd::min(rawCoordinate.GetX(), rawCoordinate.GetY()), rawCoordinate.GetZ()),
            AZStd::min(
                AZStd::min(static_cast<float>(grid.m_dims[0] - 1) - rawCoordinate.GetX(),
                           static_cast<float>(grid.m_dims[1] - 1) - rawCoordinate.GetY()),
                static_cast<float>(grid.m_dims[2] - 1) - rawCoordinate.GetZ()));
        const float gridWeight = AZStd::clamp(edgeDistance, 0.0f, 1.0f);

        Vector3 value = Vector3::CreateZero();
        float totalWeight = 0.0f;
        const int32_t originCell[3] = {
            static_cast<int32_t>(std::floor(grid.m_origin.GetX() / grid.m_spacing)),
            static_cast<int32_t>(std::floor(grid.m_origin.GetY() / grid.m_spacing)),
            static_cast<int32_t>(std::floor(grid.m_origin.GetZ() / grid.m_spacing))
        };
        for (int32_t dz = 0; dz <= 1; ++dz)
        {
            for (int32_t dy = 0; dy <= 1; ++dy)
            {
                for (int32_t dx = 0; dx <= 1; ++dx)
                {
                    const int32_t local[3] = {
                        AZStd::min(base[0] + dx, static_cast<int32_t>(grid.m_dims[0]) - 1),
                        AZStd::min(base[1] + dy, static_cast<int32_t>(grid.m_dims[1]) - 1),
                        AZStd::min(base[2] + dz, static_cast<int32_t>(grid.m_dims[2]) - 1)
                    };
                    const int32_t cellX = originCell[0] + local[0];
                    const int32_t cellY = originCell[1] + local[1];
                    const int32_t cellZ = originCell[2] + local[2];
                    const ProbeGiEntry& probe = probes[ProbeGridStorageIndex(grid, cellX, cellY, cellZ)];
                    if (!ProbeCellMatches(probe, cellX, cellY, cellZ))
                    {
                        continue;
                    }
                    const float triWeight = (dx ? frac[0] : 1.0f - frac[0]) *
                        (dy ? frac[1] : 1.0f - frac[1]) * (dz ? frac[2] : 1.0f - frac[2]);
                    const Vector3 probePosition = grid.m_origin + Vector3(
                        static_cast<float>(local[0]) * grid.m_spacing,
                        static_cast<float>(local[1]) * grid.m_spacing,
                        static_cast<float>(local[2]) * grid.m_spacing);
                    const Vector3 direction = (probePosition - biasedPosition).GetNormalizedSafe();
                    const float wrap = AZStd::max(0.05f, (direction.Dot(n) + 1.0f) * 0.5f);
                    const float weight = triWeight * wrap * wrap;
                    value += ShEvaluateIrradianceOverPi(probe.m_sh, n) * weight;
                    totalWeight += weight;
                }
            }
        }
        return { totalWeight > 0.0f ? value / totalWeight : Vector3::CreateZero(), gridWeight };
    }

    Vector3 ShadeProbeRayHit(
        const Vector3& albedo,
        const Vector3& hitNormal,
        const Vector3& rayDirection,
        const Vector3& sunDirection,
        const Vector3& sunIlluminanceOverPi,
        bool sunVisible,
        const Vector3& bounceValue)
    {
        const Vector3 normal = hitNormal.GetNormalizedSafe();
        if (normal.Dot(rayDirection) > 0.0f)
        {
            return Vector3::CreateZero();
        }
        const float cosine = AZStd::max(0.0f, normal.Dot(-sunDirection.GetNormalizedSafe()));
        const Vector3 direct = sunIlluminanceOverPi * (sunVisible ? cosine : 0.0f);
        return Vector3(albedo.GetX() * (direct.GetX() + bounceValue.GetX()),
                       albedo.GetY() * (direct.GetY() + bounceValue.GetY()),
                       albedo.GetZ() * (direct.GetZ() + bounceValue.GetZ()));
    }

    void UpdateProbe(
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
        const ProbeBounceSampler& sampleBounce)
    {
        const bool reset = !ProbeCellMatches(probe, cellX, cellY, cellZ);
        ShL1Rgb accumulated;
        const Vector3 normalizedSun = sunDirection.GetNormalizedSafe();
        for (const Vector3& rayDirection : rayDirections)
        {
            const Vector3 direction = rayDirection.GetNormalizedSafe();
            Vector3 radiance = Vector3::CreateZero();
            const BvhRayHit hit = bvh.IntersectClosest(probePosition, direction, 1.0e6f);
            if (hit.m_hit && hit.m_primitiveId < bvh.GetOrderedTriangles().size())
            {
                const auto primitiveIndex = AZStd::find(bvh.GetPrimitiveIndices().begin(), bvh.GetPrimitiveIndices().end(), hit.m_primitiveId);
                const BvhTriangle& triangle = bvh.GetOrderedTriangles()[primitiveIndex - bvh.GetPrimitiveIndices().begin()];
                const Vector3 normal = (triangle.m_v1 - triangle.m_v0).Cross(triangle.m_v2 - triangle.m_v0).GetNormalizedSafe();
                const Vector3 hitPosition = probePosition + direction * hit.m_t;
                const bool sunVisible = !bvh.IntersectAny(hitPosition + normal * 1.0e-3f, -normalizedSun, 1.0e6f);
                const Vector3 bounce = sampleBounce ? sampleBounce(hitPosition, normal) : Vector3::CreateZero();
                const Vector3 albedo = hit.m_primitiveId < primitiveAlbedoRgba8.size()
                    ? DecodeAlbedo(primitiveAlbedoRgba8[hit.m_primitiveId])
                    : Vector3(0.5f);
                radiance = ShadeProbeRayHit(albedo, normal, direction, normalizedSun, sunIlluminanceOverPi, sunVisible, bounce);
            }
            else if (sampleEnvironment)
            {
                radiance = sampleEnvironment(direction) * std::exp2(iblExposure);
            }
            ShAccumulate(accumulated, ShProjectRadiance(direction, radiance));
        }
        if (!rayDirections.empty())
        {
            ShScale(accumulated, 4.0f * Pi / static_cast<float>(rayDirections.size()));
        }

        if (!reset)
        {
            const float blend = 1.0f - AZStd::clamp(hysteresis, 0.0f, 1.0f);
            ShScale(probe.m_sh, 1.0f - blend);
            ShScale(accumulated, blend);
            ShAccumulate(probe.m_sh, accumulated);
        }
        else
        {
            probe.m_sh = accumulated;
        }
        probe.m_worldCell[0] = cellX;
        probe.m_worldCell[1] = cellY;
        probe.m_worldCell[2] = cellZ;
        probe.m_valid = true;
    }
} // namespace AZ
