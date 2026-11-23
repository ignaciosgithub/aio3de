--
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- For complete copyright and license terms please see the LICENSE at the root of this distribution.
--
-- SPDX-License-Identifier: Apache-2.0 OR MIT
--

local TrackMath = require("Scripts.Racing.TrackMath")
local TrackData = require("Scripts.Racing.TrackData")

local ArcadeCar = {
    Properties = {
        UsePlayerInput = { default = true },
        CarIndex = { default = 0.0 },
        GridLane = { default = 0.0 },
    },
}

local function clamp(value, minimum, maximum)
    return math.max(minimum, math.min(maximum, value))
end

local function approach(value, target, amount)
    if value < target then
        return math.min(target, value + amount)
    end
    return math.max(target, value - amount)
end

local function connectInput(self, eventName, field, scale)
    local callback = {
        OnPressed = function(_, value)
            self.controls[field] = value * scale
        end,
        OnHeld = function(_, value)
            self.controls[field] = value * scale
        end,
        OnReleased = function()
            self.controls[field] = 0.0
        end,
    }
    table.insert(self.callbackTables, callback)
    table.insert(
        self.handlers,
        InputEventNotificationBus.Connect(callback, InputEventNotificationId(eventName))
    )
end

local function worldPosition(entityId)
    return TransformBus.Event.GetWorldTranslation(entityId)
end

local function atan2(y, x)
    if math.atan2 then
        return math.atan2(y, x)
    end
    return math.atan(y, x)
end

local function raycastGround(self, origin, down, length)
    if self.physicsScene == nil then
        return nil
    end
    local request = SceneQueries.CreateRayCastRequest(origin, down, length, "All")
    request.ReportMultipleHits = true
    local hits = self.physicsScene:QueryScene(request)
    for index = 1, hits.HitArray:Size() do
        local hit = hits.HitArray[index]
        if hit.EntityId ~= self.entityId then
            local name = GameEntityContextRequestBus.Broadcast.GetEntityName(hit.EntityId)
            if not string.match(name or "", "^Car") then
                return hit
            end
        end
    end
    return nil
end

function ArcadeCar:OnActivate()
    if self.Properties.CarIndex == 0.0 then
        Debug.Log(string.format(
            "RACING TrackData require ok points=%d width=%.1f",
            #TrackData.points,
            TrackData.width
        ))
    end
    self.controls = { throttle = 0.0, steer = 0.0 }
    self.raceState = 0.0
    self.finished = false
    self.upsideDownTime = 0.0
    self.lastTrackIndex = nil
    self.callbackTables = {}
    self.handlers = {}
    self.tickHandler = TickBus.Connect(self)

    if self.Properties.UsePlayerInput then
        connectInput(self, "MoveForward", "throttle", 1.0)
        connectInput(self, "MoveRight", "steer", 1.0)
        local resetCallback = {
            OnPressed = function()
                self:Reset()
            end,
            OnHeld = function() end,
            OnReleased = function() end,
        }
        table.insert(self.callbackTables, resetCallback)
        table.insert(
            self.handlers,
            InputEventNotificationBus.Connect(
                resetCallback, InputEventNotificationId("Jump")
            )
        )
    else
        local throttleCallback = {
            OnEventBegin = function(_, value) self.controls.throttle = value end,
            OnEventUpdating = function(_, value) self.controls.throttle = value end,
            OnEventEnd = function() self.controls.throttle = 0.0 end,
        }
        local steerCallback = {
            OnEventBegin = function(_, value) self.controls.steer = value end,
            OnEventUpdating = function(_, value) self.controls.steer = value end,
            OnEventEnd = function() self.controls.steer = 0.0 end,
        }
        local resetCallback = {
            OnEventBegin = function() self:Reset() end,
        }
        for _, callback in ipairs({ throttleCallback, steerCallback, resetCallback }) do
            table.insert(self.callbackTables, callback)
        end
        table.insert(
            self.handlers,
            GameplayNotificationBus.Connect(
                throttleCallback,
                GameplayNotificationId(self.entityId, "Throttle", "float")
            )
        )
        table.insert(
            self.handlers,
            GameplayNotificationBus.Connect(
                steerCallback,
                GameplayNotificationId(self.entityId, "Steer", "float")
            )
        )
        table.insert(
            self.handlers,
            GameplayNotificationBus.Connect(
                resetCallback,
                GameplayNotificationId(self.entityId, "Reset", "float")
            )
        )
    end

    self.raceCallback = {
        OnEventBegin = function(_, value) self.raceState = value end,
    }
    self.finishedCallback = {
        OnEventBegin = function()
            self.finished = true
            self.raceState = 2.0
        end,
    }
    table.insert(self.callbackTables, self.raceCallback)
    table.insert(self.callbackTables, self.finishedCallback)
    table.insert(
        self.handlers,
        GameplayNotificationBus.Connect(
            self.raceCallback, GameplayNotificationId(self.entityId, "RaceState", "float")
        )
    )
    table.insert(
        self.handlers,
        GameplayNotificationBus.Connect(
            self.finishedCallback, GameplayNotificationId(self.entityId, "Finished", "float")
        )
    )

    local physics = GetPhysicsSystem()
    if physics ~= nil then
        self.physicsScene = physics:GetScene(physics:GetSceneHandle(DefaultPhysicsSceneName))
    end
    self.mass = 1200.0
    local ok, mass = pcall(function()
        return RigidBodyRequestBus.Event.GetMass(self.entityId)
    end)
    if ok and mass ~= nil and mass > 0.0 then
        self.mass = mass
    end
    local quarterMass = self.mass / 4.0
    local omega = 2.0 * math.pi * 3.0
    self.spring = quarterMass * omega * omega
    self.damper = 2.0 * 0.5 * quarterMass * omega
end

function ArcadeCar:OnDeactivate()
    if self.tickHandler then
        self.tickHandler:Disconnect()
    end
    for _, handler in ipairs(self.handlers) do
        handler:Disconnect()
    end
    self.callbackTables = nil
end

function ArcadeCar:Reset()
    local position = worldPosition(self.entityId)
    local index = TrackMath.nearestIndex(position, self.lastTrackIndex)
    self.lastTrackIndex = index
    local progress = TrackMath.progress(position, index)
    local center = TrackMath.positionAtProgress(progress)
    local tangent = TrackMath.tangent(index)
    local side = Vector3(-tangent.y, tangent.x, 0.0)
    local target = center + side * self.Properties.GridLane + Vector3(0.0, 0.0, 1.0)
    local yaw = atan2(-tangent.x, tangent.y)
    TransformBus.Event.SetWorldTranslation(self.entityId, target)
    TransformBus.Event.SetLocalRotation(self.entityId, Vector3(0.0, 0.0, yaw))
    RigidBodyRequestBus.Event.SetLinearVelocity(self.entityId, Vector3(0.0, 0.0, 0.0))
    RigidBodyRequestBus.Event.SetAngularVelocity(self.entityId, Vector3(0.0, 0.0, 0.0))
    self.upsideDownTime = 0.0
    Debug.Log(string.format("RACING reset car=%d", self.Properties.CarIndex))
end

function ArcadeCar:ApplySuspension(dt, transform, position, right, forward, down)
    local grounded = 0
    local corners = {
        { -0.8, -1.5 },
        { 0.8, -1.5 },
        { -0.8, 1.5 },
        { 0.8, 1.5 },
    }
    for _, corner in ipairs(corners) do
        local origin = position + right * corner[1] + forward * corner[2]
        local hit = raycastGround(self, origin, down, 0.95)
        if hit ~= nil then
            grounded = grounded + 1
            local velocity = RigidBodyRequestBus.Event.GetLinearVelocityAtWorldPoint(
                self.entityId, hit.Position
            )
            local compression = 0.6 - hit.Distance
            local springVelocity = velocity.x * transform:GetBasisZ().x
                + velocity.y * transform:GetBasisZ().y
                + velocity.z * transform:GetBasisZ().z
            local force = self.spring * compression - self.damper * springVelocity
            if force > 0.0 then
                RigidBodyRequestBus.Event.ApplyLinearImpulseAtWorldPoint(
                    self.entityId, transform:GetBasisZ() * (force * dt), hit.Position
                )
            end
        end
    end
    return grounded
end

function ArcadeCar:OnTick(deltaTime)
    local dt = math.min(deltaTime, 0.1)
    local transform = TransformBus.Event.GetWorldTM(self.entityId)
    local position = worldPosition(self.entityId)
    local forward = transform:GetBasisY()
    local right = transform:GetBasisX()
    local up = transform:GetBasisZ()
    local grounded = self:ApplySuspension(dt, transform, position, right, forward, -up)

    if up.z < 0.3 then
        self.upsideDownTime = self.upsideDownTime + dt
    else
        self.upsideDownTime = 0.0
    end
    if position.z < -10.0 or self.upsideDownTime >= 2.0 then
        self:Reset()
        return
    end

    local index = TrackMath.nearestIndex(position, self.lastTrackIndex)
    self.lastTrackIndex = index
    local lateral = TrackMath.lateralOffset(position, index)
    local offRoad = math.abs(lateral) > TrackData.width * 0.5 + 1.0
    local maxSpeed = offRoad and 55.0 * 0.45 or 55.0
    local reverseLimit = offRoad and 12.0 * 0.45 or 12.0
    local velocity = RigidBodyRequestBus.Event.GetLinearVelocity(self.entityId)
    local forwardSpeed = velocity.x * forward.x + velocity.y * forward.y + velocity.z * forward.z
    local throttle = clamp(self.controls.throttle, -1.0, 1.0)
    local steer = clamp(self.controls.steer, -1.0, 1.0)
    local locked = self.raceState ~= 1.0 or self.finished
    local targetSpeed = forwardSpeed

    if locked then
        targetSpeed = approach(forwardSpeed, 0.0, 24.0 * dt)
        throttle = 0.0
        steer = 0.0
    elseif grounded >= 2 then
        targetSpeed = forwardSpeed + throttle * 12.0 * dt
    else
        throttle = 0.0
        steer = 0.0
    end
    targetSpeed = clamp(targetSpeed, -reverseLimit, maxSpeed)
    velocity = velocity + forward * (targetSpeed - forwardSpeed)

    local lateralSpeed = velocity.x * right.x + velocity.y * right.y + velocity.z * right.z
    local lateralRemoval = 1.0 - (1.0 - 0.9) ^ (dt * 60.0)
    velocity = velocity - right * (lateralSpeed * lateralRemoval)
    if offRoad then
        velocity = velocity * math.max(0.0, 1.0 - 1.8 * dt)
    end
    RigidBodyRequestBus.Event.SetLinearVelocity(self.entityId, velocity)

    local oldAngular = RigidBodyRequestBus.Event.GetAngularVelocity(self.entityId)
    local yawRate = 0.0
    if not locked and grounded >= 2 then
        yawRate = -steer * 1.6 * clamp(forwardSpeed / 10.0, -1.0, 1.0)
    end
    RigidBodyRequestBus.Event.SetAngularVelocity(
        self.entityId, Vector3(oldAngular.x, oldAngular.y, yawRate)
    )
end

return ArcadeCar
