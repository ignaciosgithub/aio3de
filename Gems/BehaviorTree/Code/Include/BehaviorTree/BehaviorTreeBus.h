/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Component/ComponentBus.h>
#include <AzCore/EBus/EBus.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/RTTI/TypeInfoSimple.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>

namespace BehaviorTree
{
    enum class NodeStatus : AZ::u8
    {
        Success = 0,
        Failure = 1,
        Running = 2
    };

    class BehaviorTreeRequests
        : public AZ::ComponentBus
    {
    public:
        virtual void SetBlackboardBool(const AZStd::string& name, bool value) = 0;
        virtual void SetBlackboardFloat(const AZStd::string& name, float value) = 0;
        virtual void SetBlackboardString(const AZStd::string& name, const AZStd::string& value) = 0;
        virtual void SetBlackboardVector3(const AZStd::string& name, const AZ::Vector3& value) = 0;
        virtual void SetBlackboardEntityId(const AZStd::string& name, const AZ::EntityId& value) = 0;

        virtual bool GetBlackboardBool(const AZStd::string& name) = 0;
        virtual float GetBlackboardFloat(const AZStd::string& name) = 0;
        virtual AZStd::string GetBlackboardString(const AZStd::string& name) = 0;
        virtual AZ::Vector3 GetBlackboardVector3(const AZStd::string& name) = 0;
        virtual AZ::EntityId GetBlackboardEntityId(const AZStd::string& name) = 0;
        virtual bool HasBlackboardKey(const AZStd::string& name) = 0;

        virtual void Restart() = 0;
        virtual void SetPaused(bool paused) = 0;
        virtual bool IsRunning() = 0;
        virtual AZStd::vector<AZStd::string> GetActiveNodePaths() = 0;
    };
    using BehaviorTreeRequestBus = AZ::EBus<BehaviorTreeRequests>;

    class BehaviorTreeActionNotifications
        : public AZ::ComponentBus
    {
    public:
        virtual int OnActionTick(const AZStd::string& action, float deltaTime) = 0;
        virtual void OnActionAbort(const AZStd::string& action) = 0;
        virtual bool OnCondition(const AZStd::string& condition) = 0;
    };
    using BehaviorTreeActionNotificationBus = AZ::EBus<BehaviorTreeActionNotifications>;
} // namespace BehaviorTree

namespace AZ
{
    AZ_TYPE_INFO_SPECIALIZE(BehaviorTree::NodeStatus, "{74DB4E63-7C49-4F15-AF3A-07C6E8C08349}");
}
