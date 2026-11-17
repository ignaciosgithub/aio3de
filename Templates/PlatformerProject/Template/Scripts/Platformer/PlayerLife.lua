--
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- For complete copyright and license terms please see the LICENSE at the root of this distribution.
--
-- SPDX-License-Identifier: Apache-2.0 OR MIT
--

local PlayerLife =
{
    Properties =
    {
        MaxHealth = { default = 3 },
        KillZ = { default = -20.0 },
    },
}

function PlayerLife:OnActivate()
    self.health = self.Properties.MaxHealth
    self.checkpointIndex = 0
    self.checkpointPosition = TransformBus.Event.GetWorldTranslation(self.entityId)
    self.dead = false
    self.damageHandler = GameplayNotificationBus.Connect(
        self, GameplayNotificationId(self.entityId, "Damage", "float"))
    self.checkpointCallback = {
        OnEventBegin = function(_, index) self:OnCheckpoint(index) end,
    }
    self.checkpointHandler = GameplayNotificationBus.Connect(
        self.checkpointCallback,
        GameplayNotificationId(self.entityId, "Checkpoint", "float"))
    self.tickHandler = TickBus.Connect(self)
    self:BroadcastHealth()
end

function PlayerLife:BroadcastHealth()
    GameplayNotificationBus.Event.OnEventBegin(
        GameplayNotificationId(self.entityId, "LifeChanged", "float"), self.health)
end

function PlayerLife:OnEventBegin(value)
    if self.dead then
        return
    end
    local damage = value
    if damage == nil or damage <= 0.0 then
        damage = 1.0
    end
    self.health = math.max(0.0, self.health - damage)
    self:BroadcastHealth()
    if self.health <= 0 then
        self:Die("health")
    end
end

function PlayerLife:OnCheckpoint(index)
    if self.dead then
        return
    end
    self.checkpointIndex = index
    self.checkpointPosition =
        TransformBus.Event.GetWorldTranslation(self.entityId) + Vector3(0.0, 0.0, 1.0)
    Debug.Log(string.format("PLATFORMER checkpoint index=%d", index))
end

function PlayerLife:Die(reason)
    if self.dead then
        return
    end
    self.dead = true
    Debug.Log(string.format("PLATFORMER death reason=%s", reason))
    GameplayNotificationBus.Event.OnEventBegin(
        GameplayNotificationId(self.entityId, "PlayerDied", "float"), 1.0)
    self:Respawn()
end

function PlayerLife:Respawn()
    TransformBus.Event.SetWorldTranslation(self.entityId, self.checkpointPosition)
    CharacterGameplayRequestBus.Event.SetFallingVelocity(
        self.entityId, Vector3(0.0, 0.0, 0.0))
    self.health = self.Properties.MaxHealth
    self.dead = false
    self:BroadcastHealth()
    Debug.Log(string.format("PLATFORMER respawn checkpoint=%d", self.checkpointIndex))
    GameplayNotificationBus.Event.OnEventBegin(
        GameplayNotificationId(self.entityId, "Respawned", "float"), 1.0)
end

function PlayerLife:OnTick()
    if not self.dead and TransformBus.Event.GetWorldTranslation(self.entityId).z < self.Properties.KillZ then
        self:Die("kill-z")
    end
end

function PlayerLife:OnDeactivate()
    if self.tickHandler then self.tickHandler:Disconnect() end
    if self.damageHandler then self.damageHandler:Disconnect() end
    if self.checkpointHandler then self.checkpointHandler:Disconnect() end
    self.checkpointCallback = nil
end

return PlayerLife
