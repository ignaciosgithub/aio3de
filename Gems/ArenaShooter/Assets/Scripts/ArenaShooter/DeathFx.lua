-- 
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- For complete copyright and license terms please see the LICENSE at the root of this distribution.
-- 
-- SPDX-License-Identifier: Apache-2.0 OR MIT
-- 

-- ArenaShooter: death animation + sound. Put on the character entity that has
-- Actor + Anim Graph components (same entity as AnimationDriver.lua).
-- Listens for the "Killed"/"Respawned" gameplay events broadcast by Health.lua
-- and drives a "Dead" float anim graph parameter (1 = dead, 0 = alive) -
-- create that parameter in the Animation Editor and transition into/out of
-- your death state on it. Optionally plays a death sound through a MiniAudio
-- Playback entity (enable spatialization on it so other players hear the
-- death positionally).
local DeathFx =
{
    Properties =
    {
        SourceEntity = { default = EntityId(), description = "Entity emitting Killed/Respawned events (the player root); this entity if unset" },
        DeathSoundEntity = { default = EntityId(), description = "Optional entity with a MiniAudio Playback component to play on death" },
    },
}

function DeathFx:OnActivate()
    local source = self.Properties.SourceEntity
    if not source:IsValid() then
        source = self.entityId
    end

    self.killedHandler = GameplayNotificationBus.Connect(
        { OnEventBegin = function(_, v) self:OnKilled() end },
        GameplayNotificationId(source, "Killed", "float"))
    self.respawnHandler = GameplayNotificationBus.Connect(
        { OnEventBegin = function(_, v) self:OnRespawned() end },
        GameplayNotificationId(source, "Respawned", "float"))
end

function DeathFx:OnKilled()
    AnimGraphComponentRequestBus.Event.SetNamedParameterFloat(self.entityId, "Dead", 1.0)
    if self.Properties.DeathSoundEntity:IsValid() then
        MiniAudioPlaybackRequestBus.Event.Play(self.Properties.DeathSoundEntity)
    end
end

function DeathFx:OnRespawned()
    AnimGraphComponentRequestBus.Event.SetNamedParameterFloat(self.entityId, "Dead", 0.0)
end

function DeathFx:OnDeactivate()
    self.killedHandler:Disconnect()
    self.respawnHandler:Disconnect()
end

return DeathFx
