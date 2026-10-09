----------------------------------------------------------------------------------------------------
--
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- For complete copyright and license terms please see the LICENSE at the root of this distribution.
--
-- SPDX-License-Identifier: Apache-2.0 OR MIT
--

-- PathFollower.lua: moves this entity along a VoxelNav path to a target entity.
--
-- Setup: add a Lua Script component with this script, set the Target property to the entity to walk to,
-- and make sure the level contains a Voxel Nav Volume (VoxelNav gem) that covers both entities.
-- The entity is moved by writing its transform; swap MoveTowards for your character controller if you have one.
local PathFollower = {
    Properties = {
        Target = { default = EntityId(), description = "Entity to walk to" },
        Speed = { default = 3.0, description = "Movement speed in m/s" },
        RepathInterval = { default = 0.5, description = "Seconds between path refreshes" },
        ArriveDistance = { default = 0.25, description = "Distance at which a waypoint counts as reached" },
    },
}

function PathFollower:OnActivate()
    self.path = {}
    self.waypoint = 1
    self.repathTimer = 0.0
    self.tickHandler = TickBus.Connect(self)
end

function PathFollower:OnDeactivate()
    if self.tickHandler then
        self.tickHandler:Disconnect()
        self.tickHandler = nil
    end
end

function PathFollower:Repath()
    if not self.Properties.Target:IsValid() or not VoxelNavRequestBus.Broadcast.IsReady() then
        return
    end
    local start = TransformBus.Event.GetWorldTranslation(self.entityId)
    local goal = TransformBus.Event.GetWorldTranslation(self.Properties.Target)
    -- FindPath returns a vector of Vector3 waypoints (empty when no path exists or the volume is still baking).
    local path = VoxelNavRequestBus.Broadcast.FindPath(start, goal)
    self.path = {}
    for i = 1, #path do
        self.path[i] = path[i]
    end
    self.waypoint = 2 -- waypoint 1 is our own position
end

function PathFollower:OnTick(deltaTime, timePoint)
    self.repathTimer = self.repathTimer - deltaTime
    if self.repathTimer <= 0.0 then
        self.repathTimer = self.Properties.RepathInterval
        self:Repath()
    end

    if self.waypoint > #self.path then
        return
    end

    local position = TransformBus.Event.GetWorldTranslation(self.entityId)
    local target = self.path[self.waypoint]
    local toTarget = target - position
    local distance = toTarget:GetLength()
    if distance <= self.Properties.ArriveDistance then
        self.waypoint = self.waypoint + 1
        return
    end

    local step = math.min(distance, self.Properties.Speed * deltaTime)
    local newPosition = position + toTarget * (step / distance)
    TransformBus.Event.SetWorldTranslation(self.entityId, newPosition)

    -- Face the direction of travel (yaw only).
    local flat = Vector3(toTarget.x, toTarget.y, 0.0)
    if flat:GetLength() > 0.001 then
        local yaw = math.atan(flat.x, flat.y)
        TransformBus.Event.SetWorldRotationQuaternion(self.entityId, Quaternion.CreateRotationZ(-yaw))
    end
end

return PathFollower
