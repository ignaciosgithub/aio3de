--
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- For complete copyright and license terms please see the LICENSE at the root of this distribution.
--
-- SPDX-License-Identifier: Apache-2.0 OR MIT
--

local GuardSenses =
{
    Properties =
    {
        Target = { default = EntityId() },
    },
}

function GuardSenses:OnActivate()
    self.actionHandler = BehaviorTreeActionNotificationBus.Connect(self, self.entityId)
    self.tickHandler = TickBus.Connect(self)
    self.targetVisible = nil
end

function GuardSenses:OnDeactivate()
    self.actionHandler:Disconnect()
    self.tickHandler:Disconnect()
end

function GuardSenses:OnTick(deltaTime, timePoint)
    local target = self.Properties.Target
    local targetVisible = target:IsValid()
    if targetVisible then
        local targetPosition = TransformBus.Event.GetWorldTranslation(target)
        local guardPosition = TransformBus.Event.GetWorldTranslation(self.entityId)
        local offset = targetPosition - guardPosition
        targetVisible = offset:GetLength() <= 15.0
    end

    if targetVisible ~= self.targetVisible then
        self.targetVisible = targetVisible
        self:SetAlert(targetVisible)
        BehaviorTreeRequestBus.Event.SetBlackboardEntityId(self.entityId, "target", target)
    end
end

function GuardSenses:OnCondition(condition)
    if condition == "CanSeeTarget" then
        return self.targetVisible == true
    end
    return false
end

function GuardSenses:OnActionTick(action, deltaTime)
    if action == "Shout" then
        Debug.Log("Guard shouts")
        return BehaviorTreeStatus.Success
    end
    return BehaviorTreeStatus.Failure
end

function GuardSenses:OnActionAbort(action)
    if action == "Shout" then
        Debug.Log("Guard stopped shouting")
    end
end

function GuardSenses:SetAlert(value)
    BehaviorTreeRequestBus.Event.SetBlackboardBool(self.entityId, "alert", value)
end

return GuardSenses
