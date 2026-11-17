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
        SightRange = { default = 10.0 },
        TouchRange = { default = 1.2 },
        Damage = { default = 1.0 },
        EyeHeight = { default = 1.4 },
        PatrolOffset = { default = Vector3(4.0, 0.0, 0.0) },
    },
}

local function worldPosition(entityId)
    return TransformBus.Event.GetWorldTranslation(entityId)
end

function EnemyBrain:OnActivate()
    self.dead = false
    self.visible = false
    self.distanceToPlayer = math.huge
    self.touchCooldown = 0.0
    self.playerId = EntityId()
    self.playerBeaconCallback = {
        OnEventBegin = function(_, playerId)
            if playerId:IsValid() then
                self.playerId = playerId
            end
        end,
    }
    self.playerBeaconHandler = GameplayNotificationBus.Connect(
        self.playerBeaconCallback,
        GameplayNotificationId(EntityId(), "PlatformerPlayer", "EntityId"))
    self.stompCallback = { OnEventBegin = function() self:OnStomp() end }
    self.stompHandler = GameplayNotificationBus.Connect(
        self.stompCallback,
        GameplayNotificationId(self.entityId, "Stomp", "float"))
    self.actionHandler = BehaviorTreeActionNotificationBus.Connect(self, self.entityId)
    self.tickHandler = TickBus.Connect(self)
    self.killedCallback = { OnEventBegin = function() self:OnKilled() end }
    self.killedHandler = GameplayNotificationBus.Connect(
        self.killedCallback,
        GameplayNotificationId(self.entityId, "Killed", "float"))
    self.respawnedCallback = { OnEventBegin = function() self:OnRespawned() end }
    self.respawnedHandler = GameplayNotificationBus.Connect(
        self.respawnedCallback,
        GameplayNotificationId(self.entityId, "Respawned", "float"))

    local start = worldPosition(self.entityId)
    BehaviorTreeRequestBus.Event.SetBlackboardVector3(
        self.entityId, "patrolA", start - self.Properties.PatrolOffset)
    BehaviorTreeRequestBus.Event.SetBlackboardVector3(
        self.entityId, "patrolB", start + self.Properties.PatrolOffset)
    BehaviorTreeRequestBus.Event.SetBlackboardBool(self.entityId, "alert", false)
end

function EnemyBrain:OnDeactivate()
    if self.tickHandler then self.tickHandler:Disconnect() end
    if self.actionHandler then self.actionHandler:Disconnect() end
    if self.killedHandler then self.killedHandler:Disconnect() end
    if self.respawnedHandler then self.respawnedHandler:Disconnect() end
    if self.playerBeaconHandler then self.playerBeaconHandler:Disconnect() end
    if self.stompHandler then self.stompHandler:Disconnect() end
    self.playerBeaconCallback = nil
    self.stompCallback = nil
    self.killedCallback = nil
    self.respawnedCallback = nil
end

function EnemyBrain:CanSeePlayer(player)
    local targetPosition = worldPosition(player) + Vector3(0.0, 0.0, 1.4)
    local eyePosition = worldPosition(self.entityId) + Vector3(0.0, 0.0, self.Properties.EyeHeight)
    local offset = targetPosition - eyePosition
    local distance = offset:GetLength()
    self.distanceToPlayer = distance
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
    for index = 1, hits.HitArray:Size() do
        local hit = hits.HitArray[index]
        if hit.EntityId ~= self.entityId then
            return hit.EntityId == player
        end
    end
    return false
end

function EnemyBrain:OnTick(deltaTime)
    if self.dead then
        self.visible = false
        BehaviorTreeRequestBus.Event.SetBlackboardBool(self.entityId, "alert", false)
        return
    end

    self.touchCooldown = math.max(0.0, self.touchCooldown - deltaTime)
    if self.playerId == nil or not self.playerId:IsValid() then
        self.visible = false
        self.distanceToPlayer = math.huge
        BehaviorTreeRequestBus.Event.SetBlackboardBool(self.entityId, "alert", false)
        return
    end

    local player = self.playerId
    self.visible = self:CanSeePlayer(player)
    BehaviorTreeRequestBus.Event.SetBlackboardBool(self.entityId, "alert", self.visible)
    if self.visible then
        BehaviorTreeRequestBus.Event.SetBlackboardVector3(
            self.entityId, "target", worldPosition(player))
    end
end

function EnemyBrain:OnCondition(condition)
    if condition == "InTouchRange" then
        return not self.dead and self.visible and self.distanceToPlayer <= self.Properties.TouchRange
    end
    return false
end

function EnemyBrain:OnActionTick(action)
    if self.dead or action ~= "Touch" or not self.visible
        or self.distanceToPlayer > self.Properties.TouchRange then
        return BehaviorTreeStatus.Failure
    end

    if self.touchCooldown <= 0.0 then
        if self.playerId == nil or not self.playerId:IsValid() then
            return BehaviorTreeStatus.Failure
        end
        GameplayNotificationBus.Event.OnEventBegin(
            GameplayNotificationId(self.playerId, "Damage", "float"), self.Properties.Damage)
        self.touchCooldown = 1.0
    end
    return BehaviorTreeStatus.Running
end

function EnemyBrain:OnActionAbort()
end

function EnemyBrain:OnStomp()
    if self.dead then
        return
    end
    if self.playerId ~= nil and self.playerId:IsValid() then
        GameplayNotificationBus.Event.OnEventBegin(
            GameplayNotificationId(self.playerId, "Stomped", "float"), 1.0)
    end
    GameplayNotificationBus.Event.OnEventBegin(
        GameplayNotificationId(self.entityId, "Damage", "float"), 100.0)
end

function EnemyBrain:OnKilled()
    if self.dead then
        return
    end
    self.dead = true
    self.visible = false
    BehaviorTreeRequestBus.Event.SetBlackboardBool(self.entityId, "alert", false)
    BehaviorTreeRequestBus.Event.SetPaused(self.entityId, true)
    Debug.Log("PLATFORMER stomp enemy killed")
    GameEntityContextRequestBus.Broadcast.DeactivateGameEntity(self.entityId)
end

function EnemyBrain:OnRespawned()
    self.dead = false
    self.visible = false
    BehaviorTreeRequestBus.Event.SetBlackboardBool(self.entityId, "alert", false)
    BehaviorTreeRequestBus.Event.SetPaused(self.entityId, false)
    BehaviorTreeRequestBus.Event.Restart(self.entityId)
end

return EnemyBrain
