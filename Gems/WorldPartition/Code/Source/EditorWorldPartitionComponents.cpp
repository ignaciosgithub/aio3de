/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "EditorWorldPartitionComponents.h"

#include <AzCore/Asset/AssetSerializer.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace WorldPartition
{
    void EditorWorldPartitionComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<EditorWorldPartitionComponent, AzToolsFramework::Components::EditorComponentBase>()
                ->Version(1)
                ->Field("CellSize", &EditorWorldPartitionComponent::m_cellSize)
                ->Field("LoadRadius", &EditorWorldPartitionComponent::m_loadRadius)
                ->Field("UnloadRadius", &EditorWorldPartitionComponent::m_unloadRadius)
                ->Field("MaxCellLoadsPerFrame", &EditorWorldPartitionComponent::m_maxCellLoadsPerFrame)
                ->Field("StreamByDefault", &EditorWorldPartitionComponent::m_streamByDefault)
                ->Field("TwoDimensional", &EditorWorldPartitionComponent::m_twoDimensional)
                ->Field("Cells", &EditorWorldPartitionComponent::m_cells);
            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<EditorWorldPartitionComponent>("World Partition", "Streams level prefab entities in a cell grid")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")->Attribute(AZ::Edit::Attributes::Category, "World")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EditorWorldPartitionComponent::m_cellSize, "Cell size", "Grid cell size in meters.")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EditorWorldPartitionComponent::m_loadRadius, "Load radius", "Distance to load cells.")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EditorWorldPartitionComponent::m_unloadRadius, "Unload radius", "Distance to unload cells; clamped to the load radius.")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EditorWorldPartitionComponent::m_maxCellLoadsPerFrame, "Max cell loads per frame", "Streaming load budget.")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EditorWorldPartitionComponent::m_streamByDefault, "Stream by default", "Stream roots unless marked AlwaysLoaded.")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EditorWorldPartitionComponent::m_twoDimensional, "2D grid", "Use X/Y cells and ignore Z for distance.");
            }
        }
    }
    void EditorWorldPartitionComponent::BuildGameEntity(AZ::Entity* entity)
    {
        if (auto* runtime = entity->CreateComponent<WorldPartitionComponent>())
        {
            runtime->m_cellSize = m_cellSize;
            runtime->m_loadRadius = m_loadRadius;
            runtime->m_unloadRadius = m_unloadRadius;
            runtime->m_maxCellLoadsPerFrame = m_maxCellLoadsPerFrame;
            runtime->m_streamByDefault = m_streamByDefault;
            runtime->m_twoDimensional = m_twoDimensional;
            runtime->m_cells = m_cells;
        }
    }
    void EditorWorldPartitionComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("WorldPartitionService"));
    }
    void EditorWorldPartitionComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("WorldPartitionService"));
    }

    void EditorWorldPartitionStreamingSourceComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<EditorWorldPartitionStreamingSourceComponent, AzToolsFramework::Components::EditorComponentBase>()
                ->Version(1)->Field("LoadRadius", &EditorWorldPartitionStreamingSourceComponent::m_loadRadius);
            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<EditorWorldPartitionStreamingSourceComponent>("World Partition Streaming Source", "Adds this entity as a streaming source")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")->Attribute(AZ::Edit::Attributes::Category, "World")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EditorWorldPartitionStreamingSourceComponent::m_loadRadius, "Load radius", "Zero uses the partition radius.");
            }
        }
    }
    void EditorWorldPartitionStreamingSourceComponent::BuildGameEntity(AZ::Entity* entity)
    {
        if (auto* runtime = entity->CreateComponent<WorldPartitionStreamingSourceComponent>())
        {
            runtime->m_loadRadius = m_loadRadius;
        }
    }
    void EditorWorldPartitionStreamingSourceComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("WorldPartitionStreamingSourceService"));
    }
    void EditorWorldPartitionStreamingSourceComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("WorldPartitionStreamingSourceService"));
    }

    void EditorWorldPartitionPolicyComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<EditorWorldPartitionPolicyComponent, AzToolsFramework::Components::EditorComponentBase>()
                ->Version(1)->Field("Policy", &EditorWorldPartitionPolicyComponent::m_policy);
            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<EditorWorldPartitionPolicyComponent>("World Partition Policy", "Controls whether this subtree can be streamed")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")->Attribute(AZ::Edit::Attributes::Category, "World")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                    ->DataElement(AZ::Edit::UIHandlers::ComboBox, &EditorWorldPartitionPolicyComponent::m_policy, "Policy", "Default follows partition settings; AlwaysLoaded prevents extraction; Streamed opts in.");
            }
        }
    }
    void EditorWorldPartitionPolicyComponent::BuildGameEntity(AZ::Entity* entity)
    {
        if (auto* runtime = entity->CreateComponent<WorldPartitionPolicyComponent>())
        {
            runtime->m_policy = m_policy;
        }
    }
    void EditorWorldPartitionPolicyComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("WorldPartitionPolicyService"));
    }
    void EditorWorldPartitionPolicyComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("WorldPartitionPolicyService"));
    }
}
