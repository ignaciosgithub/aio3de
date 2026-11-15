/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzToolsFramework/ToolsComponents/EditorComponentBase.h>
#include "WorldPartitionComponent.h"
#include "WorldPartitionPolicyComponent.h"
#include "WorldPartitionStreamingSourceComponent.h"

namespace WorldPartition
{
    class EditorWorldPartitionComponent : public AzToolsFramework::Components::EditorComponentBase
    {
    public:
        AZ_EDITOR_COMPONENT(EditorWorldPartitionComponent, "{A891E9A8-2565-49EF-B4F7-A247A60A38D8}");
        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);
        void BuildGameEntity(AZ::Entity* gameEntity) override;
        float m_cellSize = 64.0f;
        float m_loadRadius = 128.0f;
        float m_unloadRadius = 160.0f;
        uint32_t m_maxCellLoadsPerFrame = 2;
        bool m_streamByDefault = true;
        bool m_twoDimensional = true;
        AZStd::vector<WorldPartitionCell> m_cells;
    };

    class EditorWorldPartitionStreamingSourceComponent : public AzToolsFramework::Components::EditorComponentBase
    {
    public:
        AZ_EDITOR_COMPONENT(EditorWorldPartitionStreamingSourceComponent, "{C9D7D986-6260-46AA-BEC2-B159838D2C7E}");
        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);
        void BuildGameEntity(AZ::Entity* gameEntity) override;
        float m_loadRadius = 0.0f;
    };

    class EditorWorldPartitionPolicyComponent : public AzToolsFramework::Components::EditorComponentBase
    {
    public:
        AZ_EDITOR_COMPONENT(EditorWorldPartitionPolicyComponent, "{FB6E8A6E-19EF-47CB-A121-F8462F3348B1}");
        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);
        void BuildGameEntity(AZ::Entity* gameEntity) override;
        WorldPartitionPolicy m_policy = WorldPartitionPolicy::Default;
    };
}
