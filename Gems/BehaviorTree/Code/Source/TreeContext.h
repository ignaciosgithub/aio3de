/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Component/EntityId.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>

namespace BehaviorTree
{
    class TreeContext
    {
    public:
        virtual ~TreeContext() = default;

        virtual AZ::Vector3 GetPosition() const = 0;
        virtual AZStd::vector<AZ::Vector3> FindPath(const AZ::Vector3& from, const AZ::Vector3& to) = 0;
        virtual void MoveToward(const AZ::Vector3& direction, float speed) = 0;
        virtual void StopMoving() = 0;
        virtual bool GetEntityPosition(const AZ::EntityId& entityId, AZ::Vector3& outPosition) const = 0;
        virtual int TickAction(const AZStd::string& action, float deltaTime) = 0;
        virtual void AbortAction(const AZStd::string& action) = 0;
        virtual bool EvaluateCondition(const AZStd::string& condition) = 0;
        virtual void Log(const AZStd::string& message) = 0;
    };
} // namespace BehaviorTree
