/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "TreeInstance.h"

namespace BehaviorTree
{
    TreeInstance::TreeInstance(BehaviorTreeDefinition definition, AZ::u32 randomSeed)
        : m_definition(AZStd::move(definition))
        , m_root(AZStd::make_unique<RuntimeNode>(m_definition.m_root, "0"))
        , m_randomState(randomSeed)
    {
        InitializeBlackboard();
    }

    void TreeInstance::InitializeBlackboard()
    {
        m_blackboard.Clear();
        for (const auto& [key, entry] : m_definition.m_blackboard)
        {
            m_blackboard.Set(key, entry.m_value);
        }
    }

    NodeStatus TreeInstance::Tick(float deltaTime, TreeContext& context)
    {
        if (m_complete)
        {
            return m_finalStatus;
        }
        if (m_restartNextTick)
        {
            m_root->Reset(context);
            m_restartNextTick = false;
        }

        const float safeDeltaTime = AZStd::max(deltaTime, 0.0f);
        m_elapsed += safeDeltaTime;
        NodeTick tick{ m_blackboard, context, safeDeltaTime, m_elapsed, m_randomState };
        const NodeStatus status = m_root->Tick(tick);
        m_running = status == NodeStatus::Running;
        if (!m_running)
        {
            m_finalStatus = status;
            if (m_definition.m_loop)
            {
                m_restartNextTick = true;
                m_running = true;
            }
            else
            {
                m_complete = true;
            }
        }
        return status;
    }

    void TreeInstance::Restart(TreeContext& context)
    {
        m_root->Reset(context);
        m_elapsed = 0.0f;
        m_finalStatus = NodeStatus::Running;
        m_running = true;
        m_complete = false;
        m_restartNextTick = false;
    }

    bool TreeInstance::IsRunning() const
    {
        return m_running;
    }

    bool TreeInstance::IsComplete() const
    {
        return m_complete;
    }

    Blackboard& TreeInstance::GetBlackboard()
    {
        return m_blackboard;
    }

    const Blackboard& TreeInstance::GetBlackboard() const
    {
        return m_blackboard;
    }

    AZStd::vector<AZStd::string> TreeInstance::GetActiveNodePaths() const
    {
        AZStd::vector<AZStd::string> paths;
        if (m_root)
        {
            m_root->CollectActiveNodePaths(paths);
        }
        return paths;
    }

    const BehaviorTreeDefinition& TreeInstance::GetDefinition() const
    {
        return m_definition;
    }
} // namespace BehaviorTree
