-- ArenaShooter: health, death and respawn. Put on the player and on targets/bots.
-- Listens for "Damage" gameplay events on this entity (sent by Weapon.lua).
-- On death: teleports the entity to SpawnPoint (or its start position), restores
-- health, and broadcasts a "Killed" gameplay event on this entity so score/AI
-- systems can react.
local Health =
{
    Properties =
    {
        MaxHealth = { default = 100.0 },
        SpawnPoint = { default = EntityId(), description = "Optional respawn location; entity start position if unset" },
        RespawnDelay = { default = 2.0, suffix = " s" },
    },
}

function Health:OnActivate()
    self.health = self.Properties.MaxHealth
    self.respawnTimer = -1.0
    self.startTM = TransformBus.Event.GetWorldTM(self.entityId)

    self.damageHandler = GameplayNotificationBus.Connect(
        self, GameplayNotificationId(self.entityId, "Damage", "float"))
    self.tickHandler = TickBus.Connect(self)
end

function Health:OnEventBegin(damage)
    if self.respawnTimer >= 0.0 then
        return -- already dead, waiting on respawn
    end
    self.health = self.health - damage
    if self.health <= 0.0 then
        self:Die()
    end
end

function Health:Die()
    self.respawnTimer = self.Properties.RespawnDelay
    GameplayNotificationBus.Event.OnEventBegin(
        GameplayNotificationId(self.entityId, "Killed", "float"), 1.0)
    -- the body stays in place while dead (it takes no further damage) so a
    -- death animation can play — see DeathFx.lua
end

function Health:OnTick(deltaTime, timePoint)
    if self.respawnTimer >= 0.0 then
        self.respawnTimer = self.respawnTimer - deltaTime
        if self.respawnTimer < 0.0 then
            self:Respawn()
        end
    end
end

function Health:Respawn()
    self.health = self.Properties.MaxHealth
    local tm = self.startTM
    if self.Properties.SpawnPoint:IsValid() then
        tm = TransformBus.Event.GetWorldTM(self.Properties.SpawnPoint)
    end
    TransformBus.Event.SetWorldTM(self.entityId, tm)
    GameplayNotificationBus.Event.OnEventBegin(
        GameplayNotificationId(self.entityId, "Respawned", "float"), 1.0)
end

function Health:OnDeactivate()
    self.tickHandler:Disconnect()
    self.damageHandler:Disconnect()
end

return Health
