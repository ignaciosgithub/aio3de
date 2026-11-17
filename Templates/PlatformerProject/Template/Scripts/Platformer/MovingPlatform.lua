--
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- For complete copyright and license terms please see the LICENSE at the root of this distribution.
--
-- SPDX-License-Identifier: Apache-2.0 OR MIT
--

local MovingPlatform =
{
    Properties =
    {
        Offset = { default = Vector3(8.0, 0.0, 0.0) },
        Period = { default = 4.0 },
    },
}

function MovingPlatform:OnActivate()
    RigidBodyRequestBus.Event.SetKinematic(self.entityId, true)
    self.startPosition = TransformBus.Event.GetWorldTranslation(self.entityId)
    self.elapsed = 0.0
    self.tickHandler = TickBus.Connect(self)
end

function MovingPlatform:OnTick(deltaTime)
    self.elapsed = self.elapsed + deltaTime
    local period = math.max(self.Properties.Period, 0.1)
    local phase = 2.0 * math.pi * (self.elapsed % period) / period
    local eased = (1.0 - math.cos(phase)) * 0.5
    local position = self.startPosition + self.Properties.Offset * eased
    RigidBodyRequestBus.Event.SetKinematicTarget(
        self.entityId, Transform.CreateTranslation(position))
end

function MovingPlatform:OnDeactivate()
    if self.tickHandler then self.tickHandler:Disconnect() end
end

return MovingPlatform
