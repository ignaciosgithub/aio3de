/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Math/Aabb.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/base.h>
#include <AzCore/std/containers/vector.h>

namespace VoxelNav
{
    //! Integer voxel coordinate. Voxel (x, y, z) spans [x, x+1) x [y, y+1) x [z, z+1) in grid units.
    struct GridCoord
    {
        AZ::s32 m_x = 0;
        AZ::s32 m_y = 0;
        AZ::s32 m_z = 0;

        GridCoord() = default;
        GridCoord(AZ::s32 x, AZ::s32 y, AZ::s32 z)
            : m_x(x)
            , m_y(y)
            , m_z(z)
        {
        }

        bool operator==(const GridCoord& other) const
        {
            return m_x == other.m_x && m_y == other.m_y && m_z == other.m_z;
        }
        bool operator!=(const GridCoord& other) const
        {
            return !(*this == other);
        }
        GridCoord operator+(const GridCoord& other) const
        {
            return GridCoord(m_x + other.m_x, m_y + other.m_y, m_z + other.m_z);
        }
        GridCoord operator-(const GridCoord& other) const
        {
            return GridCoord(m_x - other.m_x, m_y - other.m_y, m_z - other.m_z);
        }
    };

    //! Compact bit set with one bit per voxel.
    class VoxelBits
    {
    public:
        void Resize(AZ::u64 count, bool value);
        void Fill(bool value);
        AZ::u64 Size() const
        {
            return m_count;
        }
        bool Test(AZ::u64 index) const
        {
            return (m_words[index >> 6] >> (index & 63)) & 1ull;
        }
        void Set(AZ::u64 index, bool value)
        {
            const AZ::u64 mask = 1ull << (index & 63);
            if (value)
            {
                m_words[index >> 6] |= mask;
            }
            else
            {
                m_words[index >> 6] &= ~mask;
            }
        }
        //! Number of set bits.
        AZ::u64 PopCount() const;

    private:
        AZStd::vector<AZ::u64> m_words;
        AZ::u64 m_count = 0;
    };

    //! Regular voxel lattice over a world-space box; each voxel is either solid (intersects collision) or free.
    class VoxelGrid
    {
    public:
        //! Hard cap on the number of voxels of a single grid (128M voxels = 16 MB per bit layer).
        static constexpr AZ::u64 MaxVoxelCount = 1ull << 27;

        //! Sizes the grid so that it covers the bounds with voxels of the given edge length. Returns false (and
        //! leaves the grid invalid) when the bounds or voxel size are degenerate or exceed MaxVoxelCount.
        bool Initialize(const AZ::Aabb& bounds, float voxelSize);

        bool IsValid() const
        {
            return m_voxelCount > 0;
        }

        AZ::s32 SizeX() const
        {
            return m_sizeX;
        }
        AZ::s32 SizeY() const
        {
            return m_sizeY;
        }
        AZ::s32 SizeZ() const
        {
            return m_sizeZ;
        }
        AZ::u64 VoxelCount() const
        {
            return m_voxelCount;
        }
        float VoxelSize() const
        {
            return m_voxelSize;
        }
        const AZ::Vector3& Origin() const
        {
            return m_origin;
        }
        //! Bounds actually covered by the grid (the requested bounds rounded up to whole voxels).
        AZ::Aabb Bounds() const;

        bool InBounds(const GridCoord& coord) const
        {
            return coord.m_x >= 0 && coord.m_y >= 0 && coord.m_z >= 0 && coord.m_x < m_sizeX && coord.m_y < m_sizeY &&
                coord.m_z < m_sizeZ;
        }

        AZ::u64 Index(const GridCoord& coord) const
        {
            return static_cast<AZ::u64>(coord.m_x) +
                static_cast<AZ::u64>(m_sizeX) * (static_cast<AZ::u64>(coord.m_y) + static_cast<AZ::u64>(m_sizeY) * static_cast<AZ::u64>(coord.m_z));
        }
        GridCoord Coord(AZ::u64 index) const;

        //! Voxel containing the world position (may be out of bounds; see InBounds).
        GridCoord WorldToVoxel(const AZ::Vector3& position) const;
        GridCoord ClampToGrid(const GridCoord& coord) const;

        AZ::Vector3 VoxelMin(const GridCoord& coord) const;
        AZ::Vector3 VoxelCenter(const GridCoord& coord) const;
        //! Center of the bottom face: where a walking agent's feet stand.
        AZ::Vector3 VoxelBottomCenter(const GridCoord& coord) const;
        AZ::Aabb VoxelAabb(const GridCoord& coord) const;

        //! Out-of-bounds voxels report as not solid.
        bool IsSolid(const GridCoord& coord) const
        {
            return InBounds(coord) && m_solid.Test(Index(coord));
        }
        void SetSolid(const GridCoord& coord, bool solid)
        {
            if (InBounds(coord))
            {
                m_solid.Set(Index(coord), solid);
            }
        }
        void ClearSolid();
        AZ::u64 SolidCount() const
        {
            return m_solid.PopCount();
        }

        const VoxelBits& SolidBits() const
        {
            return m_solid;
        }

    private:
        AZ::Vector3 m_origin = AZ::Vector3::CreateZero();
        float m_voxelSize = 0.0f;
        AZ::s32 m_sizeX = 0;
        AZ::s32 m_sizeY = 0;
        AZ::s32 m_sizeZ = 0;
        AZ::u64 m_voxelCount = 0;
        VoxelBits m_solid;
    };
} // namespace VoxelNav
