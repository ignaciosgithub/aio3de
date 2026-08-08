-- 
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- For complete copyright and license terms please see the LICENSE at the root of this distribution.
-- 
-- SPDX-License-Identifier: Apache-2.0 OR MIT
-- 

-- ArenaShooter: in-game settings menu (DebugDraw overlay; requires the
-- DebugDraw and GameSettings gems). Put on the game manager entity.
-- Toggle with F10 / gamepad start ("MenuToggle"), navigate with arrow keys /
-- d-pad ("MenuUp"/"MenuDown"), adjust values with left/right ("MenuLeft"/
-- "MenuRight"), rebind keys with Enter / gamepad A ("MenuSelect") then press
-- the new key/button. Changes apply live (open it any time, e.g. during
-- warm-up or the map vote) and persist via the GameSettings gem.
local SettingsMenu =
{
    Properties =
    {
        CameraEntity = { default = EntityId(), description = "Camera whose FOV the fov setting drives" },
    },
}

local rows = {
    { kind = "value", label = "FOV",                 setting = "fov",              default = 75.0, step = 5.0,  min = 50.0, max = 120.0 },
    { kind = "value", label = "Mouse sensitivity",   setting = "mouse_sensitivity", default = 1.0, step = 0.1,  min = 0.1,  max = 5.0 },
    { kind = "value", label = "Gamepad sensitivity", setting = "pad_sensitivity",   default = 1.0, step = 0.1,  min = 0.1,  max = 5.0 },
    { kind = "bind",  label = "Shoot",   key = "bind_shoot",   default = "mouse_button_left" },
    { kind = "bind",  label = "Jump",    key = "bind_jump",    default = "keyboard_key_edit_space" },
    { kind = "bind",  label = "Next weapon", key = "bind_weapon_next", default = "keyboard_key_alphanumeric_E" },
    { kind = "bind",  label = "Prev weapon", key = "bind_weapon_prev", default = "keyboard_key_alphanumeric_Q" },
}

local function connectPress(eventName, fn)
    return InputEventNotificationBus.Connect({
        OnPressed = function(_, v) fn(v) end,
        OnHeld = function(_, v) end,
        OnReleased = function(_, v) end,
    }, InputEventNotificationId(eventName))
end

function SettingsMenu:OnActivate()
    self.open = false
    self.row = 1

    self.handlers = {
        connectPress("MenuToggle", function() self.open = not self.open end),
        connectPress("MenuUp", function() if self.open then self.row = ((self.row - 2) % #rows) + 1 end end),
        connectPress("MenuDown", function() if self.open then self.row = (self.row % #rows) + 1 end end),
        connectPress("MenuLeft", function() self:Adjust(-1) end),
        connectPress("MenuRight", function() self:Adjust(1) end),
        connectPress("MenuSelect", function() self:Select() end),
    }
    self.tickHandler = TickBus.Connect(self)
    self:ApplyFov()
end

function SettingsMenu:Adjust(direction)
    if not self.open then return end
    local row = rows[self.row]
    if row.kind ~= "value" then return end
    local value = GameSettingsRequestBus.Broadcast.GetValue(row.setting, row.default)
    value = math.max(row.min, math.min(row.max, value + direction * row.step))
    GameSettingsRequestBus.Broadcast.SetValue(row.setting, value)
    GameSettingsRequestBus.Broadcast.Save()
    if row.setting == "fov" then
        self:ApplyFov()
    end
end

function SettingsMenu:Select()
    if not self.open then return end
    local row = rows[self.row]
    if row.kind == "bind" then
        GameSettingsRequestBus.Broadcast.StartRebind(row.key)
    end
end

function SettingsMenu:ApplyFov()
    if self.Properties.CameraEntity:IsValid() then
        local fov = GameSettingsRequestBus.Broadcast.GetValue("fov", 75.0)
        CameraRequestBus.Event.SetFovDegrees(self.Properties.CameraEntity, fov)
    end
end

function SettingsMenu:OnTick(deltaTime, timePoint)
    if not self.open then return end

    DebugDrawRequestBus.Broadcast.DrawTextOnScreen("=== SETTINGS (F10/start to close) ===", Color(1.0, 1.0, 0.2, 1.0), 0.0)
    local rebinding = GameSettingsRequestBus.Broadcast.IsRebinding()
    for i, row in ipairs(rows) do
        local marker = (i == self.row) and "> " or "  "
        local text
        if row.kind == "value" then
            local value = GameSettingsRequestBus.Broadcast.GetValue(row.setting, row.default)
            text = string.format("%s%s: %.2f  (left/right)", marker, row.label, value)
        else
            local channel = GameSettingsRequestBus.Broadcast.GetBinding(row.key, row.default)
            if rebinding and i == self.row then
                text = string.format("%s%s: <press new key/button>", marker, row.label)
            else
                text = string.format("%s%s: %s  (enter/A to rebind)", marker, row.label, channel)
            end
        end
        local color = (i == self.row) and Color(1.0, 1.0, 1.0, 1.0) or Color(0.7, 0.7, 0.7, 1.0)
        DebugDrawRequestBus.Broadcast.DrawTextOnScreen(text, color, 0.0)
    end
end

function SettingsMenu:OnDeactivate()
    self.tickHandler:Disconnect()
    for _, handler in ipairs(self.handlers) do
        handler:Disconnect()
    end
end

return SettingsMenu
