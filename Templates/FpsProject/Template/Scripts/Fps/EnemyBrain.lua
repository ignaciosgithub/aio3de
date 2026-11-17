--
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- For complete copyright and license terms please see the LICENSE at the root of this distribution.
--
-- SPDX-License-Identifier: Apache-2.0 OR MIT
--

local EnemyBrain =
{
    Properties =
    {
        Player = { default = EntityId() },
        SightRange = { default = 25.0 },
        AttackRange = { default = 12.0 },
        FireInterval = { default = 0.6 },
        Damage = { default = 8.0 },
        EyeHeight = { default = 1.5 },
        PatrolA = { default = EntityId() },
        PatrolB = { default = EntityId() },
    },
}

local function worldPosition(entityId)
    return TransformBus.Event.GetWorldTranslation(entityId)
end

function EnemyBrain:OnActivate()
    self.dead = false
    self.visible = false
    self.distanceToTarget = math.huge
    self.shotTimer = 0.0
    self.actionHandler = BehaviorTreeActionNotificationBus.Connect(self, self.entityId)
    self.tickHandler = TickBus.Connect(self)
    self.killedHandler = GameplayNotificationBus.Connect(
        { OnEventBegin = function(_, value) self:OnKilled(value) end },
        GameplayNotificationId(self.entityId, "Killed", "float"))
    self.respawnedHandler = GameplayNotificationBus.Connect(
        { OnEventBegin = function(_, value) self:OnRespawned(value) end },
        GameplayNotificationId(self.entityId, "Respawned", "float"))

    local start = worldPosition(self.entityId)
    local patrolA = self.Properties.PatrolA
    local patrolB = self.Properties.PatrolB
    self.patrolA = patrolA:IsValid() and worldPosition(patrolA) or start + Vector3(-6.0, 0.0, 0.0)
    self.patrolB = patrolB:IsValid() and worldPosition(patrolB) or start + Vector3(6.0, 0.0, 0.0)
    BehaviorTreeRequestBus.Event.SetBlackboardVector3(self.entityId, "patrolA", self.patrolA)
    BehaviorTreeRequestBus.Event.SetBlackboardVector3(self.entityId, "patrolB", self.patrolB)
    BehaviorTreeRequestBus.Event.SetBlackboardBool(self.entityId, "alert", false)
    BehaviorTreeRequestBus.Event.SetBlackboardEntityId(self.entityId, "target", self.Properties.Player)
end

function EnemyBrain:OnDeactivate()
    if self.tickHandler then self.tickHandler:Disconnect() end
    if self.actionHandler then self.actionHandler:Disconnect() end
    if self.killedHandler then self.killedHandler:Disconnect() end
    if self.respawnedHandler then self.respawnedHandler:Disconnect() end
end

function EnemyBrain:OnKilled()
    self.dead = true
    self.visible = false
    self.distanceToTarget = math.huge
    BehaviorTreeRequestBus.Event.SetBlackboardBool(self.entityId, "alert", false)
    BehaviorTreeRequestBus.Event.SetPaused(self.entityId, true)
end

function EnemyBrain:OnRespawned()
    self.dead = false
    self.visible = false
    self.distanceToTarget = math.huge
    BehaviorTreeRequestBus.Event.SetBlackboardBool(self.entityId, "alert", false)
    BehaviorTreeRequestBus.Event.SetPaused(self.entityId, false)
    BehaviorTreeRequestBus.Event.Restart(self.entityId)
end

function EnemyBrain:CanSeePlayer()
    local target = self.Properties.Player
    if not target:IsValid() then
        return false
    end

    local targetPosition = worldPosition(target) + Vector3(0.0, 0.0, 1.4)
    local eyePosition = worldPosition(self.entityId) + Vector3(0.0, 0.0, self.Properties.EyeHeight)
    local offset = targetPosition - eyePosition
    local distance = offset:GetLength()
    self.distanceToTarget = distance
    if distance > self.Properties.SightRange or distance <= 0.001 then
        return false
    end

    local physics = GetPhysicsSystem()
    local scene = physics:GetScene(physics:GetSceneHandle(DefaultPhysicsSceneName))
    if scene == nil then
        return false
    end
    local request = SceneQueries.CreateRayCastRequest(eyePosition, offset / distance, distance, "All")
    local hits = scene:QueryScene(request)
    for i = 1, #hits.HitArray do
        local hit = hits.HitArray[i]
        if hit.EntityId ~= self.entityId then
            return hit.EntityId == target
        end
    end
    return false
end

function EnemyBrain:OnTick(deltaTime)
    if self.dead then
        self.visible = false
        self.distanceToTarget = math.huge
        BehaviorTreeRequestBus.Event.SetBlackboardBool(self.entityId, "alert", false)
        return
    end

    self.visible = self:CanSeePlayer()
    BehaviorTreeRequestBus.Event.SetBlackboardBool(self.entityId, "alert", self.visible)
    BehaviorTreeRequestBus.Event.SetBlackboardEntityId(self.entityId, "target", self.Properties.Player)
    BehaviorTreeRequestBus.Event.SetBlackboardVector3(self.entityId, "patrolA", self.patrolA)
    BehaviorTreeRequestBus.Event.SetBlackboardVector3(self.entityId, "patrolB", self.patrolB)
    self.shotTimer = math.max(0.0, self.shotTimer - deltaTime)
end

function EnemyBrain:OnCondition(condition)
    if condition == "InRange" then
        return not self.dead and self.visible and self.distanceToTarget <= self.Properties.AttackRange
    end
    return false
end

function EnemyBrain:OnActionTick(action, deltaTime)
    if self.dead or action ~= "Shoot" or not self.visible or self.distanceToTarget > self.Properties.AttackRange then
        return BehaviorTreeStatus.Failure
    end

    if self.shotTimer <= 0.0 then
        local target = self.Properties.Player
        local targetPosition = worldPosition(target) + Vector3(0.0, 0.0, 1.4)
        local start = worldPosition(self.entityId) + Vector3(0.0, 0.0, self.Properties.EyeHeight)
        local offset = targetPosition - start
        local distance = offset:GetLength()
        local direction = offset / math.max(distance, 0.001)
        local physics = GetPhysicsSystem()
        local scene = physics:GetScene(physics:GetSceneHandle(DefaultPhysicsSceneName))
        if scene ~= nil then
            local hits = scene:QueryScene(SceneQueries.CreateRayCastRequest(start, direction, distance, "All"))
            for i = 1, #hits.HitArray do
                local hit = hits.HitArray[i]
                if hit.EntityId ~= self.entityId then
                    if hit.EntityId == target then
                        GameplayNotificationBus.Event.OnEventBegin(
                            GameplayNotificationId(hit.EntityId, "Damage", "float"), self.Properties.Damage)
                        DebugDrawRequestBus.Broadcast.DrawLineLocationToLocation(
                            start, hit.Position, Color(1.0, 0.15, 0.1, 1.0), 0.15)
                    end
                    break
                end
            end
        end
        self.shotTimer = self.Properties.FireInterval
    end
    return BehaviorTreeStatus.Running
end

function EnemyBrain:OnActionAbort(action)
    if action == "Shoot" then
        self.shotTimer = 0.0
    end
end

return EnemyBrain
