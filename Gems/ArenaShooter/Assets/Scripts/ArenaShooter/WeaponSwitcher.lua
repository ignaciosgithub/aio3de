-- 
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- For complete copyright and license terms please see the LICENSE at the root of this distribution.
-- 
-- SPDX-License-Identifier: Apache-2.0 OR MIT
-- 

-- ArenaShooter: weapon switching. Put on the same entity as the Weapon.lua
-- instances (give each Weapon a unique Slot of 1..N).
-- Cycles the active slot with the mouse scroll wheel ("WeaponScroll"), Q/E or
-- gamepad bumpers ("WeaponPrev"/"WeaponNext"), and broadcasts the selection
-- as a "WeaponSlot" gameplay event that each Weapon.lua honors.
local WeaponSwitcher =
{
    Properties =
    {
        WeaponCount = { default = 2, description = "Number of weapon slots (Weapon.lua Slot values 1..N)" },
        SwitchSoundEntity = { default = EntityId(), description = "Optional entity with a MiniAudio Playback component to play on switch" },
    },
}

function WeaponSwitcher:OnActivate()
    self.slot = 1

    self.scrollHandler = InputEventNotificationBus.Connect({
        OnPressed = function(_, value)
            if value > 0.0 then
                self:Cycle(1)
            elseif value < 0.0 then
                self:Cycle(-1)
            end
        end,
        OnHeld = function(_, value) end,
        OnReleased = function(_, value) end,
    }, InputEventNotificationId("WeaponScroll"))

    self.nextHandler = InputEventNotificationBus.Connect({
        OnPressed = function(_, value) self:Cycle(1) end,
        OnHeld = function(_, value) end,
        OnReleased = function(_, value) end,
    }, InputEventNotificationId("WeaponNext"))

    self.prevHandler = InputEventNotificationBus.Connect({
        OnPressed = function(_, value) self:Cycle(-1) end,
        OnHeld = function(_, value) end,
        OnReleased = function(_, value) end,
    }, InputEventNotificationId("WeaponPrev"))

    -- announce the initial slot once everything is active
    self.tickHandler = TickBus.Connect(self)
end

function WeaponSwitcher:OnTick(deltaTime, timePoint)
    self.tickHandler:Disconnect()
    self.tickHandler = nil
    self:Broadcast()
end

function WeaponSwitcher:Cycle(step)
    local count = math.max(self.Properties.WeaponCount, 1)
    self.slot = ((self.slot - 1 + step) % count) + 1
    self:Broadcast()
    if self.Properties.SwitchSoundEntity:IsValid() then
        MiniAudioPlaybackRequestBus.Event.Play(self.Properties.SwitchSoundEntity)
    end
end

function WeaponSwitcher:Broadcast()
    GameplayNotificationBus.Event.OnEventBegin(
        GameplayNotificationId(self.entityId, "WeaponSlot", "float"), self.slot)
end

function WeaponSwitcher:OnDeactivate()
    self.scrollHandler:Disconnect()
    self.nextHandler:Disconnect()
    self.prevHandler:Disconnect()
    if self.tickHandler then
        self.tickHandler:Disconnect()
    end
end

return WeaponSwitcher
