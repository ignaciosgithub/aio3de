/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include "BehaviorTreeDefinition.h"
#include "TreeContext.h"

#include <BehaviorTree/BehaviorTreeBus.h>

#include <AzCore/std/smart_ptr/unique_ptr.h>

namespace BehaviorTree
{
    struct NodeTick
    {
        Blackboard& m_blackboard;
        TreeContext& m_context;
        float m_deltaTime = 0.0f;
        float m_time = 0.0f;
        AZ::u32& m_randomState;
    };

    class RuntimeNode
    {
    public:
        RuntimeNode(const NodeDefinition& definition, AZStd::string path);

        NodeStatus Tick(NodeTick& tick);
        void Abort(TreeContext& context);
        void Reset(TreeContext& context);
        void CollectActiveNodePaths(AZStd::vector<AZStd::string>& paths) const;

    private:
        NodeStatus TickSequence(NodeTick& tick, bool selector, bool reactive);
        NodeStatus TickParallel(NodeTick& tick);
        NodeStatus TickDecorator(NodeTick& tick);
        NodeStatus TickLeaf(NodeTick& tick);
        void AbortRunningChildren(TreeContext& context);

        AZStd::string GetString(const AZStd::string& key) const;
        float GetFloat(const AZStd::string& key, float fallback) const;
        AZ::s32 GetInt(const AZStd::string& key, AZ::s32 fallback) const;
        bool GetBool(const AZStd::string& key, bool fallback) const;

        NodeDefinition m_definition;
        AZStd::string m_path;
        AZStd::vector<AZStd::unique_ptr<RuntimeNode>> m_children;
        AZStd::vector<NodeStatus> m_parallelStatuses;
        NodeStatus m_lastStatus = NodeStatus::Failure;
        size_t m_runningChild = 0;
        AZ::s32 m_repeatCount = 0;
        float m_elapsed = 0.0f;
        float m_startedAt = 0.0f;
        float m_cooldownUntil = 0.0f;
        float m_nextRepathTime = 0.0f;
        bool m_started = false;
        bool m_hasPath = false;
        AZStd::vector<AZ::Vector3> m_movePath;
        size_t m_waypointIndex = 0;
    };
} // namespace BehaviorTree
