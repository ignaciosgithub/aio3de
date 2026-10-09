/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "VoxelNavSettings.h"

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace VoxelNav
{
    void VoxelNavSettings::Reflect(AZ::ReflectContext* context)
    {
        auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context);
        if (!serializeContext)
        {
            return;
        }

        serializeContext->Class<VoxelNavSettings>()
            ->Version(1)
            ->Field("Size", &VoxelNavSettings::m_size)
            ->Field("VoxelSize", &VoxelNavSettings::m_voxelSize)
            ->Field("AgentMode", &VoxelNavSettings::m_agentMode)
            ->Field("AgentRadius", &VoxelNavSettings::m_agentRadius)
            ->Field("AgentHeight", &VoxelNavSettings::m_agentHeight)
            ->Field("MaxStepUp", &VoxelNavSettings::m_maxStepUp)
            ->Field("MaxStepDown", &VoxelNavSettings::m_maxStepDown)
            ->Field("IncludeDynamicBodies", &VoxelNavSettings::m_includeDynamicBodies)
            ->Field("BakeBudgetMs", &VoxelNavSettings::m_bakeBudgetMs)
            ->Field("SnapDistance", &VoxelNavSettings::m_snapDistance)
            ->Field("MaxSearchNodes", &VoxelNavSettings::m_maxSearchNodes)
            ->Field("SmoothPaths", &VoxelNavSettings::m_smoothPaths)
            ->Field("DebugDraw", &VoxelNavSettings::m_debugDraw)
            ->Field("DebugDrawPaths", &VoxelNavSettings::m_debugDrawPaths);

        AZ::EditContext* editContext = serializeContext->GetEditContext();
        if (!editContext)
        {
            return;
        }

        editContext->Class<VoxelNavSettings>("Voxel Nav Settings", "Voxel grid, agent shape and query settings")
            ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
            ->ClassElement(AZ::Edit::ClassElements::Group, "Volume")
                ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
            ->DataElement(AZ::Edit::UIHandlers::Default, &VoxelNavSettings::m_size, "Size",
                "World-space size of the volume, centered on this entity")
                ->Attribute(AZ::Edit::Attributes::Min, 0.1f)
            ->DataElement(AZ::Edit::UIHandlers::Default, &VoxelNavSettings::m_voxelSize, "Voxel size",
                "Edge length of one voxel (smaller = more accurate, more memory and bake time)")
                ->Attribute(AZ::Edit::Attributes::Min, 0.05f)
                ->Attribute(AZ::Edit::Attributes::Suffix, " m")
            ->DataElement(AZ::Edit::UIHandlers::Default, &VoxelNavSettings::m_includeDynamicBodies, "Include dynamic bodies",
                "Also treat dynamic rigid bodies present at bake time as obstacles (static colliders are always used)")
            ->DataElement(AZ::Edit::UIHandlers::Default, &VoxelNavSettings::m_bakeBudgetMs, "Bake budget (ms/frame)",
                "Spread the bake over several frames with this time budget; 0 bakes everything on the first frame")
                ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
            ->ClassElement(AZ::Edit::ClassElements::Group, "Agent")
                ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
            ->DataElement(AZ::Edit::UIHandlers::ComboBox, &VoxelNavSettings::m_agentMode, "Mode",
                "Walk: grounded agent with step limits. Fly: free 3D movement (sphere of the given radius)")
                ->EnumAttribute(AgentMode::Walk, "Walk")
                ->EnumAttribute(AgentMode::Fly, "Fly")
            ->DataElement(AZ::Edit::UIHandlers::Default, &VoxelNavSettings::m_agentRadius, "Radius",
                "Horizontal clearance kept from collision (Fly: sphere radius)")
                ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                ->Attribute(AZ::Edit::Attributes::Suffix, " m")
            ->DataElement(AZ::Edit::UIHandlers::Default, &VoxelNavSettings::m_agentHeight, "Height",
                "Walk only: vertical clearance the agent needs above its feet")
                ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                ->Attribute(AZ::Edit::Attributes::Suffix, " m")
            ->DataElement(AZ::Edit::UIHandlers::Default, &VoxelNavSettings::m_maxStepUp, "Max step up",
                "Walk only: tallest ledge the agent can climb in one step")
                ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                ->Attribute(AZ::Edit::Attributes::Suffix, " m")
            ->DataElement(AZ::Edit::UIHandlers::Default, &VoxelNavSettings::m_maxStepDown, "Max step down",
                "Walk only: highest drop the agent will take")
                ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                ->Attribute(AZ::Edit::Attributes::Suffix, " m")
            ->ClassElement(AZ::Edit::ClassElements::Group, "Queries")
                ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
            ->DataElement(AZ::Edit::UIHandlers::Default, &VoxelNavSettings::m_snapDistance, "Snap distance",
                "How far a start or goal may be moved to the nearest navigable voxel")
                ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                ->Attribute(AZ::Edit::Attributes::Suffix, " m")
            ->DataElement(AZ::Edit::UIHandlers::Default, &VoxelNavSettings::m_maxSearchNodes, "Max search nodes",
                "Upper bound of A* expansions per query (0 = unlimited)")
            ->DataElement(AZ::Edit::UIHandlers::Default, &VoxelNavSettings::m_smoothPaths, "Smooth paths",
                "String-pull paths so they contain only the corners the agent needs to turn at")
            ->ClassElement(AZ::Edit::ClassElements::Group, "Debug")
                ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
            ->DataElement(AZ::Edit::UIHandlers::ComboBox, &VoxelNavSettings::m_debugDraw, "Draw voxels",
                "Draw navigable (green) and/or solid (red) voxels in game mode")
                ->EnumAttribute(DebugDrawMode::None, "None")
                ->EnumAttribute(DebugDrawMode::Navigable, "Navigable")
                ->EnumAttribute(DebugDrawMode::Solid, "Solid")
                ->EnumAttribute(DebugDrawMode::NavigableAndSolid, "Navigable and solid")
            ->DataElement(AZ::Edit::UIHandlers::Default, &VoxelNavSettings::m_debugDrawPaths, "Draw paths",
                "Draw the most recent paths returned by this volume");
    }

    AgentParams VoxelNavSettings::ToAgentParams() const
    {
        AgentParams agent;
        agent.m_mode = m_agentMode;
        agent.m_radius = m_agentRadius;
        agent.m_height = m_agentHeight;
        agent.m_maxStepUp = m_maxStepUp;
        agent.m_maxStepDown = m_maxStepDown;
        return agent;
    }
} // namespace VoxelNav
