--
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- For complete copyright and license terms please see the LICENSE at the root of this distribution.
--
-- SPDX-License-Identifier: Apache-2.0 OR MIT
--

local TrackData = require("Scripts.Racing.TrackData")
local TrackMath = require("Scripts.Racing.TrackMath")

local AiDriver = {
    Properties = {
        SkillSpeedScale = { default = 0.93 },
        LaneOffset = { default = 0.0 },
        LookAheadMin = { default = 8.0 },
        LookAheadPerSpeed = { default = 0.6 },
        CarIndex = { default = 1.0 },
    },
}

local MAX_SPEED = 55.0

local function clamp(value, minimum, maximum)
    return math.max(minimum, math.min(maximum, value))
end

local function atan2(y, x)
    if math.atan2 then
        return math.atan2(y, x)
    end
    return math.atan(y, x)
end

function AiDriver:OnActivate()
    self.racing = false
    self.finished = false
    self.lastTrackIndex = nil
    self.aheadEntity = EntityId()
    self.aheadDistance = math.huge
    self.stuckTime = 0.0
    self.recoverReady = false
    self.recoverActive = false
    self.recoverCooldown = 0.0
    self.overtakeTime = 0.0
    self.overtakeOffset = 0.0
    self.overtakeTarget = EntityId()
    self.overtakeUsed = false
    self.callbackTables = {}

    self.raceCallback = {
        OnEventBegin = function(_, value)
            self.racing = value == 1.0
            if self.racing then
                BehaviorTreeRequestBus.Event.SetBlackboardBool(
                    self.entityId, "racing", true
                )
            end
        end,
    }
    self.finishedCallback = {
        OnEventBegin = function()
            self.racing = false
            self.finished = true
            BehaviorTreeRequestBus.Event.SetBlackboardBool(
                self.entityId, "racing", false
            )
        end,
    }
    table.insert(self.callbackTables, self.raceCallback)
    table.insert(self.callbackTables, self.finishedCallback)
    self.raceHandler = GameplayNotificationBus.Connect(
        self.raceCallback,
        GameplayNotificationId(self.entityId, "RaceState", "float")
    )
    self.finishedHandler = GameplayNotificationBus.Connect(
        self.finishedCallback,
        GameplayNotificationId(self.entityId, "Finished", "float")
    )
    self.actionHandler = BehaviorTreeActionNotificationBus.Connect(self, self.entityId)
    self.tickHandler = TickBus.Connect(self)
    BehaviorTreeRequestBus.Event.SetBlackboardBool(self.entityId, "racing", false)
end

function AiDriver:OnDeactivate()
    if self.tickHandler then
        self.tickHandler:Disconnect()
    end
    if self.actionHandler then
        self.actionHandler:Disconnect()
    end
    if self.raceHandler then
        self.raceHandler:Disconnect()
    end
    if self.finishedHandler then
        self.finishedHandler:Disconnect()
    end
    self.callbackTables = nil
end

function AiDriver:Send(eventName, value)
    GameplayNotificationBus.Event.OnEventBegin(
        GameplayNotificationId(self.entityId, eventName, "float"), value
    )
end

function AiDriver:FindCarAhead()
    local transform = TransformBus.Event.GetWorldTM(self.entityId)
    local position = TransformBus.Event.GetWorldTranslation(self.entityId)
    local forward = transform:GetBasisY()
    local origin = position + transform:GetBasisZ() * 0.6
    local physics = GetPhysicsSystem()
    local scene = physics:GetScene(physics:GetSceneHandle(DefaultPhysicsSceneName))
    if scene == nil then
        self.aheadEntity = EntityId()
        self.aheadDistance = math.huge
        return
    end
    local request = SceneQueries.CreateRayCastRequest(origin, forward, 14.0, "All")
    request.ReportMultipleHits = true
    local hits = scene:QueryScene(request)
    self.aheadEntity = EntityId()
    self.aheadDistance = math.huge
    for index = 1, hits.HitArray:Size() do
        local hit = hits.HitArray[index]
        if hit.EntityId ~= self.entityId then
            local name = GameEntityContextRequestBus.Broadcast.GetEntityName(hit.EntityId)
            if string.match(name or "", "^Car") then
                self.aheadEntity = hit.EntityId
                self.aheadDistance = hit.Distance
                return
            end
        end
    end
end

function AiDriver:ChooseOvertakeSide(position, index)
    local lateral = TrackMath.lateralOffset(position, index) + self.Properties.LaneOffset
    local leftRoom = TrackData.width * 0.5 - (lateral + 4.0)
    local rightRoom = TrackData.width * 0.5 + (lateral - 4.0)
    self.overtakeOffset = leftRoom >= rightRoom and 4.0 or -4.0
    self.overtakeTime = 3.0
end

function AiDriver:Drive(overtake)
    local position = TransformBus.Event.GetWorldTranslation(self.entityId)
    local index = TrackMath.nearestIndex(position, self.lastTrackIndex)
    self.lastTrackIndex = index
    local progress = TrackMath.progress(position, index)
    local transform = TransformBus.Event.GetWorldTM(self.entityId)
    local forward = transform:GetBasisY()
    local right = transform:GetBasisX()
    local velocity = RigidBodyRequestBus.Event.GetLinearVelocity(self.entityId)
    local speed = math.abs(velocity.x * forward.x + velocity.y * forward.y)
    local lookAhead = self.Properties.LookAheadMin
        + speed * self.Properties.LookAheadPerSpeed
    local target, tangent = TrackMath.pointAhead(progress, lookAhead)
    local laneOffset = self.Properties.LaneOffset
    if overtake then
        laneOffset = laneOffset + self.overtakeOffset
    end
    local side = Vector3(-tangent.y, tangent.x, 0.0)
    target = target + side * laneOffset
    local offset = target - position
    local localX = offset.x * right.x + offset.y * right.y
    local localY = offset.x * forward.x + offset.y * forward.y
    local targetDistanceSquared = math.max(1.0, localX * localX + localY * localY)
    local curvature = 2.0 * localX / targetDistanceSquared
    local steering = clamp(curvature * math.max(speed, 5.0) / 1.6, -1.0, 1.0)

    local targetSpeed = MAX_SPEED * self.Properties.SkillSpeedScale
    for distance = 10.0, 60.0, 10.0 do
        local _, futureTangent = TrackMath.pointAhead(progress, distance)
        local dot = clamp(
            tangent.x * futureTangent.x + tangent.y * futureTangent.y,
            -1.0,
            1.0
        )
        local turn = math.acos(dot)
        if turn > 0.001 then
            local radius = distance / turn
            targetSpeed = math.min(targetSpeed, math.sqrt(14.0 * radius))
        end
    end
    local throttle = clamp((targetSpeed - speed) * 0.18, -1.0, 1.0)
    self:Send("Throttle", throttle)
    self:Send("Steer", steering)
end

function AiDriver:OnTick(deltaTime)
    if not self.racing or self.finished then
        return
    end
    local dt = math.min(deltaTime, 0.1)
    local velocity = RigidBodyRequestBus.Event.GetLinearVelocity(self.entityId)
    local speed = velocity:GetLength()
    self.recoverCooldown = math.max(0.0, self.recoverCooldown - dt)
    if speed < 2.0 and self.recoverCooldown <= 0.0 then
        self.stuckTime = self.stuckTime + dt
        if self.stuckTime >= 2.5 then
            self.recoverReady = true
        end
    else
        self.stuckTime = 0.0
    end
    if self.overtakeTime > 0.0 then
        self.overtakeTime = math.max(0.0, self.overtakeTime - dt)
    end
    self:FindCarAhead()
end

function AiDriver:OnCondition(condition)
    if condition == "NeedsRecovery" then
        return self.racing and self.recoverReady
    elseif condition == "CarAhead" then
        local hasAhead = self.racing
            and self.aheadEntity:IsValid()
            and self.aheadDistance < 14.0
        if not hasAhead
            or (self.overtakeTarget:IsValid() and self.aheadEntity ~= self.overtakeTarget)
        then
            self.overtakeTarget = EntityId()
            self.overtakeUsed = false
            self.overtakeTime = 0.0
            self.overtakeOffset = 0.0
        end
        return hasAhead and (not self.overtakeUsed or self.overtakeTime > 0.0)
    end
    return false
end

function AiDriver:OnActionTick(action)
    if action == "Recover" then
        if self.recoverReady then
            self.recoverReady = false
            self.recoverActive = true
            self.recoverCooldown = 4.0
            self.stuckTime = 0.0
            self:Send("Reset", 1.0)
            Debug.Log(string.format("RACING AI recovery car=%.0f", self.Properties.CarIndex))
            return BehaviorTreeStatus.Running
        end
        if self.recoverActive then
            self.recoverActive = false
            return BehaviorTreeStatus.Success
        end
        return BehaviorTreeStatus.Failure
    elseif action == "Overtake" then
        if not self.racing or not self.aheadEntity:IsValid() then
            return BehaviorTreeStatus.Failure
        end
        if self.overtakeTime <= 0.0 then
            if self.overtakeUsed then
                return BehaviorTreeStatus.Failure
            end
            local position = TransformBus.Event.GetWorldTranslation(self.entityId)
            local index = TrackMath.nearestIndex(position, self.lastTrackIndex)
            self:ChooseOvertakeSide(position, index)
            self.overtakeTarget = self.aheadEntity
            self.overtakeUsed = true
        end
        self:Drive(true)
        return BehaviorTreeStatus.Running
    elseif action == "FollowLine" then
        if not self.racing then
            return BehaviorTreeStatus.Failure
        end
        self.overtakeOffset = 0.0
        self:Drive(false)
        return BehaviorTreeStatus.Running
    elseif action == "Hold" then
        self:Send("Throttle", 0.0)
        self:Send("Steer", 0.0)
        return BehaviorTreeStatus.Running
    end
    return BehaviorTreeStatus.Failure
end

function AiDriver:OnActionAbort()
end

return AiDriver
