/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <WorldPartition/WorldPartitionStreamingPlanner.h>

#include <AzCore/std/algorithm.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/limits.h>
#include <AzCore/std/math.h>
#include <algorithm>

namespace WorldPartition
{
    namespace
    {
        float DistanceToBounds(const AZ::Vector3& position, const AZ::Aabb& bounds, bool twoDimensional)
        {
            if (!bounds.IsValid())
            {
                return AZStd::numeric_limits<float>::max();
            }

            const AZ::Vector3 minimum = bounds.GetMin();
            const AZ::Vector3 maximum = bounds.GetMax();
            const float dx = AZStd::max(AZStd::max(minimum.GetX() - position.GetX(), 0.0f), position.GetX() - maximum.GetX());
            const float dy = AZStd::max(AZStd::max(minimum.GetY() - position.GetY(), 0.0f), position.GetY() - maximum.GetY());
            const float dz = twoDimensional ? 0.0f
                : AZStd::max(AZStd::max(minimum.GetZ() - position.GetZ(), 0.0f), position.GetZ() - maximum.GetZ());
            return AZStd::sqrt(dx * dx + dy * dy + dz * dz);
        }
    }

    StreamingPlan PlanStreaming(
        const AZStd::vector<CellStreamState>& cells,
        const AZStd::vector<StreamingSource>& sources,
        const StreamingSettings& settings)
    {
        StreamingPlan plan;
        if (sources.empty())
        {
            return plan;
        }

        const float baseLoad = AZStd::max(settings.m_loadRadius, 0.0f);
        const float baseUnload = AZStd::max(settings.m_unloadRadius, baseLoad);
        const float ratio = baseLoad > 0.0f ? baseUnload / baseLoad : 1.0f;
        struct Candidate
        {
            uint32_t m_index;
            float m_distance;
        };
        AZStd::vector<Candidate> candidates;

        for (uint32_t index = 0; index < cells.size(); ++index)
        {
            bool withinLoad = false;
            bool beyondUnload = true;
            float nearestDistance = AZStd::numeric_limits<float>::max();
            for (const StreamingSource& source : sources)
            {
                const float loadRadius = source.m_loadRadius > 0.0f ? source.m_loadRadius : baseLoad;
                const float unloadRadius = loadRadius * ratio;
                const float distance = DistanceToBounds(source.m_position, cells[index].m_bounds, settings.m_twoDimensional);
                nearestDistance = AZStd::min(nearestDistance, distance);
                withinLoad |= distance <= loadRadius;
                beyondUnload &= distance > unloadRadius;
            }

            if (cells[index].m_loaded)
            {
                if (beyondUnload)
                {
                    plan.m_unload.push_back(index);
                }
            }
            else if (withinLoad)
            {
                candidates.push_back({ index, nearestDistance });
            }
        }

        std::sort(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs)
        {
            return lhs.m_distance == rhs.m_distance ? lhs.m_index < rhs.m_index : lhs.m_distance < rhs.m_distance;
        });
        const size_t loadCount = AZStd::min<size_t>(candidates.size(), settings.m_maxCellLoadsPerFrame);
        for (size_t index = 0; index < loadCount; ++index)
        {
            plan.m_load.push_back(candidates[index].m_index);
        }
        return plan;
    }
}
