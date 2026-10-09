/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Math/Vector3.h>
#include <AzCore/RTTI/ReflectContext.h>
#include <AzCore/RTTI/TypeInfoSimple.h>
#include <VoxelNav/VoxelNavBus.h>

#include "VoxelNavData.h"

namespace VoxelNav
{
    enum class DebugDrawMode : AZ::u8
    {
        None = 0,
        Navigable,
        Solid,
        NavigableAndSolid,
    };

    //! Inspector tunables of a Voxel Nav Volume (shared by the editor and runtime components).
    struct VoxelNavSettings
    {
        AZ_TYPE_INFO(VoxelNavSettings, "{2B6D4E8F-1C3A-4F5B-8D7E-9A0B1C2D3E4F}");

        static void Reflect(AZ::ReflectContext* context);

        AgentParams ToAgentParams() const;

        //! World-space size of the volume, centered on the entity position.
        AZ::Vector3 m_size = AZ::Vector3(50.0f, 50.0f, 20.0f);
        float m_voxelSize = 0.5f;

        AgentMode m_agentMode = AgentMode::Walk;
        float m_agentRadius = 0.4f;
        float m_agentHeight = 1.8f;
        float m_maxStepUp = 0.5f;
        float m_maxStepDown = 1.0f;

        bool m_includeDynamicBodies = false;
        //! Milliseconds of baking per frame; 0 bakes everything on the first frame.
        float m_bakeBudgetMs = 0.0f;

        //! How far (world units) start/goal may be moved to the nearest navigable voxel.
        float m_snapDistance = 2.0f;
        AZ::u32 m_maxSearchNodes = 250000;
        bool m_smoothPaths = true;

        DebugDrawMode m_debugDraw = DebugDrawMode::None;
        bool m_debugDrawPaths = false;
    };
} // namespace VoxelNav

namespace AZ
{
    AZ_TYPE_INFO_SPECIALIZE(VoxelNav::DebugDrawMode, "{5F0A1B2C-3D4E-4F60-8192-A3B4C5D6E7F8}");
}
