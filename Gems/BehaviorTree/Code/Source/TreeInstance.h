/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include "BehaviorTreeDefinition.h"
#include "BehaviorTreeNodes.h"

#include <AzCore/std/smart_ptr/unique_ptr.h>

namespace BehaviorTree
{
    class TreeInstance
    {
    public:
        explicit TreeInstance(BehaviorTreeDefinition definition, AZ::u32 randomSeed = 0x6D2B79F5u);

        NodeStatus Tick(float deltaTime, TreeContext& context);
        void Restart(TreeContext& context);
        bool IsRunning() const;
        bool IsComplete() const;

        Blackboard& GetBlackboard();
        const Blackboard& GetBlackboard() const;
        AZStd::vector<AZStd::string> GetActiveNodePaths() const;
        const BehaviorTreeDefinition& GetDefinition() const;

    private:
        void InitializeBlackboard();

        BehaviorTreeDefinition m_definition;
        AZStd::unique_ptr<RuntimeNode> m_root;
        Blackboard m_blackboard;
        AZ::u32 m_randomState = 0;
        float m_elapsed = 0.0f;
        NodeStatus m_finalStatus = NodeStatus::Running;
        bool m_running = true;
        bool m_complete = false;
        bool m_restartNextTick = false;
    };
} // namespace BehaviorTree
