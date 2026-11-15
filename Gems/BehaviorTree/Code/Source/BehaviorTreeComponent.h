/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include "TreeContext.h"
#include "TreeInstance.h"

#include <BehaviorTree/BehaviorTreeBus.h>

#include <AzCore/Component/Component.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/std/containers/unordered_set.h>

namespace BehaviorTree
{
    class BehaviorTreeComponent
        : public AZ::Component
        , private AZ::TickBus::Handler
        , private BehaviorTreeRequestBus::Handler
        , private TreeContext
    {
    public:
        AZ_COMPONENT(BehaviorTreeComponent, "{C7CBDE41-185B-4EC7-9919-3E25C4F6FB11}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);

        void SetConfiguration(const AZStd::string& treeFile, float tickInterval, bool startActive, bool debugLogTransitions);
        void Activate() override;
        void Deactivate() override;

    private:
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;
        bool LoadTree();

        void SetBlackboardBool(const AZStd::string& name, bool value) override;
        void SetBlackboardFloat(const AZStd::string& name, float value) override;
        void SetBlackboardString(const AZStd::string& name, const AZStd::string& value) override;
        void SetBlackboardVector3(const AZStd::string& name, const AZ::Vector3& value) override;
        void SetBlackboardEntityId(const AZStd::string& name, const AZ::EntityId& value) override;
        bool GetBlackboardBool(const AZStd::string& name) override;
        float GetBlackboardFloat(const AZStd::string& name) override;
        AZStd::string GetBlackboardString(const AZStd::string& name) override;
        AZ::Vector3 GetBlackboardVector3(const AZStd::string& name) override;
        AZ::EntityId GetBlackboardEntityId(const AZStd::string& name) override;
        bool HasBlackboardKey(const AZStd::string& name) override;
        void Restart() override;
        void SetPaused(bool paused) override;
        bool IsRunning() override;
        AZStd::vector<AZStd::string> GetActiveNodePaths() override;

        AZ::Vector3 GetPosition() const override;
        AZStd::vector<AZ::Vector3> FindPath(const AZ::Vector3& from, const AZ::Vector3& to) override;
        void MoveToward(const AZ::Vector3& direction, float speed) override;
        void StopMoving() override;
        bool GetEntityPosition(const AZ::EntityId& entityId, AZ::Vector3& outPosition) const override;
        int TickAction(const AZStd::string& action, float deltaTime) override;
        void AbortAction(const AZStd::string& action) override;
        bool EvaluateCondition(const AZStd::string& condition) override;
        void Log(const AZStd::string& message) override;

        AZStd::string m_treeFile;
        float m_tickInterval = 0.0f;
        bool m_startActive = true;
        bool m_debugLogTransitions = false;

        AZStd::unique_ptr<TreeInstance> m_tree;
        AZStd::unordered_set<AZStd::string> m_warnedMissingActions;
        AZ::Vector3 m_desiredVelocity = AZ::Vector3::CreateZero();
        float m_tickAccumulator = 0.0f;
        NodeStatus m_previousStatus = NodeStatus::Failure;
        bool m_hasPreviousStatus = false;
        bool m_paused = false;
        bool m_loadErrorLogged = false;
    };
} // namespace BehaviorTree
