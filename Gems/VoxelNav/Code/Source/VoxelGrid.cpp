/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "VoxelGrid.h"

#include <AzCore/Math/MathIntrinsics.h>
#include <AzCore/Math/MathUtils.h>
#include <AzCore/std/algorithm.h>

#include <cmath>

namespace VoxelNav
{
    void VoxelBits::Resize(AZ::u64 count, bool value)
    {
        m_count = count;
        m_words.assign(static_cast<size_t>((count + 63) >> 6), value ? ~0ull : 0ull);
    }

    void VoxelBits::Fill(bool value)
    {
        AZStd::fill(m_words.begin(), m_words.end(), value ? ~0ull : 0ull);
    }

    AZ::u64 VoxelBits::PopCount() const
    {
        AZ::u64 total = 0;
        const AZ::u64 fullWords = m_count >> 6;
        for (AZ::u64 i = 0; i < fullWords; ++i)
        {
            total += static_cast<AZ::u64>(az_popcnt_u64(m_words[static_cast<size_t>(i)]));
        }
        const AZ::u64 tail = m_count & 63;
        if (tail != 0)
        {
            const AZ::u64 mask = (1ull << tail) - 1ull;
            total += static_cast<AZ::u64>(az_popcnt_u64(m_words[static_cast<size_t>(fullWords)] & mask));
        }
        return total;
    }

    bool VoxelGrid::Initialize(const AZ::Aabb& bounds, float voxelSize)
    {
        m_voxelCount = 0;
        m_sizeX = m_sizeY = m_sizeZ = 0;
        m_solid.Resize(0, false);

        if (!bounds.IsValid() || !bounds.IsFinite() || !(voxelSize > 0.0f) || !std::isfinite(voxelSize))
        {
            return false;
        }

        const AZ::Vector3 extents = bounds.GetExtents();
        if (extents.GetMinElement() <= 0.0f)
        {
            return false;
        }

        // Round up so the grid never covers less than the requested box; the tiny epsilon keeps exact
        // multiples (e.g. 10 / 0.5) from spilling into an extra layer because of float rounding.
        const double epsilon = 1e-4;
        const double sx = std::ceil(static_cast<double>(extents.GetX()) / voxelSize - epsilon);
        const double sy = std::ceil(static_cast<double>(extents.GetY()) / voxelSize - epsilon);
        const double sz = std::ceil(static_cast<double>(extents.GetZ()) / voxelSize - epsilon);
        if (sx < 1.0 || sy < 1.0 || sz < 1.0)
        {
            return false;
        }
        const double count = sx * sy * sz;
        if (count > static_cast<double>(MaxVoxelCount))
        {
            return false;
        }

        m_origin = bounds.GetMin();
        m_voxelSize = voxelSize;
        m_sizeX = static_cast<AZ::s32>(sx);
        m_sizeY = static_cast<AZ::s32>(sy);
        m_sizeZ = static_cast<AZ::s32>(sz);
        m_voxelCount = static_cast<AZ::u64>(m_sizeX) * static_cast<AZ::u64>(m_sizeY) * static_cast<AZ::u64>(m_sizeZ);
        m_solid.Resize(m_voxelCount, false);
        return true;
    }

    AZ::Aabb VoxelGrid::Bounds() const
    {
        if (!IsValid())
        {
            return AZ::Aabb::CreateNull();
        }
        return AZ::Aabb::CreateFromMinMax(
            m_origin,
            m_origin + AZ::Vector3(static_cast<float>(m_sizeX), static_cast<float>(m_sizeY), static_cast<float>(m_sizeZ)) * m_voxelSize);
    }

    GridCoord VoxelGrid::Coord(AZ::u64 index) const
    {
        const AZ::u64 sliceSize = static_cast<AZ::u64>(m_sizeX) * static_cast<AZ::u64>(m_sizeY);
        const AZ::u64 z = index / sliceSize;
        const AZ::u64 rem = index - z * sliceSize;
        const AZ::u64 y = rem / static_cast<AZ::u64>(m_sizeX);
        const AZ::u64 x = rem - y * static_cast<AZ::u64>(m_sizeX);
        return GridCoord(static_cast<AZ::s32>(x), static_cast<AZ::s32>(y), static_cast<AZ::s32>(z));
    }

    GridCoord VoxelGrid::WorldToVoxel(const AZ::Vector3& position) const
    {
        const AZ::Vector3 local = (position - m_origin) / m_voxelSize;
        auto toCell = [](float v) -> AZ::s32
        {
            const float f = std::floor(v);
            // Keep far-away points representable instead of overflowing the integer conversion.
            return static_cast<AZ::s32>(AZ::GetClamp(f, -1.0e9f, 1.0e9f));
        };
        return GridCoord(toCell(local.GetX()), toCell(local.GetY()), toCell(local.GetZ()));
    }

    GridCoord VoxelGrid::ClampToGrid(const GridCoord& coord) const
    {
        return GridCoord(
            AZ::GetClamp(coord.m_x, 0, m_sizeX - 1), AZ::GetClamp(coord.m_y, 0, m_sizeY - 1), AZ::GetClamp(coord.m_z, 0, m_sizeZ - 1));
    }

    AZ::Vector3 VoxelGrid::VoxelMin(const GridCoord& coord) const
    {
        return m_origin +
            AZ::Vector3(static_cast<float>(coord.m_x), static_cast<float>(coord.m_y), static_cast<float>(coord.m_z)) * m_voxelSize;
    }

    AZ::Vector3 VoxelGrid::VoxelCenter(const GridCoord& coord) const
    {
        return VoxelMin(coord) + AZ::Vector3(0.5f * m_voxelSize);
    }

    AZ::Vector3 VoxelGrid::VoxelBottomCenter(const GridCoord& coord) const
    {
        return VoxelMin(coord) + AZ::Vector3(0.5f * m_voxelSize, 0.5f * m_voxelSize, 0.0f);
    }

    AZ::Aabb VoxelGrid::VoxelAabb(const GridCoord& coord) const
    {
        const AZ::Vector3 min = VoxelMin(coord);
        return AZ::Aabb::CreateFromMinMax(min, min + AZ::Vector3(m_voxelSize));
    }

    void VoxelGrid::ClearSolid()
    {
        m_solid.Fill(false);
    }
} // namespace VoxelNav
