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
#include <AzCore/std/containers/vector.h>
#include <cstdint>

namespace WorldPartition
{
    struct StreamingSource
    {
        AZ::Vector3 m_position = AZ::Vector3::CreateZero();
        float m_loadRadius = 0.0f;
    };

    struct CellStreamState
    {
        AZ::Aabb m_bounds = AZ::Aabb::CreateNull();
        bool m_loaded = false;
    };

    struct StreamingSettings
    {
        float m_loadRadius = 128.0f;
        float m_unloadRadius = 160.0f;
        uint32_t m_maxCellLoadsPerFrame = 2;
        bool m_twoDimensional = true;
    };

    struct StreamingPlan
    {
        AZStd::vector<uint32_t> m_load;
        AZStd::vector<uint32_t> m_unload;
    };

    StreamingPlan PlanStreaming(
        const AZStd::vector<CellStreamState>& cells,
        const AZStd::vector<StreamingSource>& sources,
        const StreamingSettings& settings);
}
