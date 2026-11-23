--
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- For complete copyright and license terms please see the LICENSE at the root of this distribution.
--
-- SPDX-License-Identifier: Apache-2.0 OR MIT
--

local ChaseCamera = {
    Properties = {
        Target = { default = EntityId() },
        Distance = { default = 8.0 },
        Height = { default = 3.0 },
        PositionSmoothing = { default = 6.0 },
        LookSmoothing = { default = 10.0 },
    },
}

local function atan2(y, x)
    if math.atan2 then
        return math.atan2(y, x)
    end
    return math.atan(y, x)
end

local function smoothAngle(current, target, amount)
    local difference = (target - current + math.pi) % (2.0 * math.pi) - math.pi
    return current + difference * amount
end

function ChaseCamera:OnActivate()
    self.currentYaw = 0.0
    self.currentPitch = 0.0
    self.tickHandler = TickBus.Connect(self)
end

function ChaseCamera:OnDeactivate()
    if self.tickHandler then
        self.tickHandler:Disconnect()
    end
end

function ChaseCamera:OnTick(deltaTime)
    if not self.Properties.Target:IsValid() then
        return
    end
    local dt = math.min(deltaTime, 0.1)
    local target = self.Properties.Target
    local carPosition = TransformBus.Event.GetWorldTranslation(target)
    local bodyTransform = TransformBus.Event.GetWorldTM(target)
    local velocity = RigidBodyRequestBus.Event.GetLinearVelocity(target)
    local direction = Vector3(velocity.x, velocity.y, 0.0)
    if direction:GetLength() < 3.0 then
        local forward = bodyTransform:GetBasisY()
        direction = Vector3(forward.x, forward.y, 0.0)
    end
    if direction:GetLength() < 0.001 then
        direction = Vector3(0.0, 1.0, 0.0)
    else
        direction = direction / direction:GetLength()
    end

    local desiredPosition = carPosition - direction * self.Properties.Distance
        + Vector3(0.0, 0.0, self.Properties.Height)
    local currentPosition = TransformBus.Event.GetWorldTranslation(self.entityId)
    local positionAlpha = 1.0 - math.exp(-self.Properties.PositionSmoothing * dt)
    TransformBus.Event.SetWorldTranslation(
        self.entityId,
        currentPosition + (desiredPosition - currentPosition) * positionAlpha
    )

    local lookPoint = carPosition + Vector3(0.0, 0.0, 1.0)
    local toTarget = lookPoint - desiredPosition
    local horizontalLength = math.sqrt(toTarget.x * toTarget.x + toTarget.y * toTarget.y)
    local targetYaw = atan2(-toTarget.x, toTarget.y)
    local targetPitch = atan2(toTarget.z, horizontalLength)
    local lookAlpha = 1.0 - math.exp(-self.Properties.LookSmoothing * dt)
    self.currentYaw = smoothAngle(self.currentYaw, targetYaw, lookAlpha)
    self.currentPitch = self.currentPitch + (targetPitch - self.currentPitch) * lookAlpha
    TransformBus.Event.SetLocalRotation(
        self.entityId, Vector3(self.currentPitch, 0.0, self.currentYaw)
    )
end

return ChaseCamera
