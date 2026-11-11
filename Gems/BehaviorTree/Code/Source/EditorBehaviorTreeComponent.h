/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzToolsFramework/ToolsComponents/EditorComponentBase.h>

namespace BehaviorTree
{
    class EditorBehaviorTreeComponent
        : public AzToolsFramework::Components::EditorComponentBase
    {
    public:
        AZ_EDITOR_COMPONENT(EditorBehaviorTreeComponent, "{103E8734-1499-4A03-A362-8875FA475B54}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);

        void Activate() override;
        void Deactivate() override;
        void BuildGameEntity(AZ::Entity* gameEntity) override;

    private:
        AZ::Crc32 OpenEditor();

        AZStd::string m_treeFile = "Gems/BehaviorTree/Examples/Guard.behaviortree.json";
        float m_tickInterval = 0.0f;
        bool m_startActive = true;
        bool m_debugLogTransitions = false;
    };
} // namespace BehaviorTree
