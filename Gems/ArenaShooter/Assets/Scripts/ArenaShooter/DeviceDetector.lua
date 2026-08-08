-- 
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- For complete copyright and license terms please see the LICENSE at the root of this distribution.
-- 
-- SPDX-License-Identifier: Apache-2.0 OR MIT
-- 

-- ArenaShooter: input device detection. Watches the "KMActivity" / "PadActivity"
-- input events (bound to keyboard/mouse and gamepad inputs respectively in
-- arenashooter.inputbindings) and broadcasts an "ActiveInputDevice" gameplay
-- event on this entity whenever the player switches device:
--   1.0 = keyboard/mouse, 2.0 = gamepad
-- Use it to swap button prompts on your UI or adjust aim assist.
local DeviceDetector =
{
    Properties =
    {
        ShowOnScreen = { default = true, description = "Show the active device as debug text (DebugDraw gem)" },
    },
}

local KEYBOARD_MOUSE = 1.0
local GAMEPAD = 2.0

function DeviceDetector:OnActivate()
    self.activeDevice = 0.0

    local function activityHandler(device)
        return {
            OnPressed = function(_, v) self:OnActivity(device) end,
            OnHeld = function(_, v) self:OnActivity(device) end,
            OnReleased = function(_, v) end,
        }
    end
    self.kmHandler = InputEventNotificationBus.Connect(activityHandler(KEYBOARD_MOUSE), InputEventNotificationId("KMActivity"))
    self.padHandler = InputEventNotificationBus.Connect(activityHandler(GAMEPAD), InputEventNotificationId("PadActivity"))
end

function DeviceDetector:OnActivity(device)
    if device ~= self.activeDevice then
        self.activeDevice = device
        GameplayNotificationBus.Event.OnEventBegin(
            GameplayNotificationId(self.entityId, "ActiveInputDevice", "float"), device)
        if self.Properties.ShowOnScreen then
            local name = (device == GAMEPAD) and "Gamepad" or "Keyboard/Mouse"
            DebugDrawRequestBus.Broadcast.DrawTextOnScreen("Input: " .. name, Color(0.6, 0.8, 1.0, 1.0), 2.0)
        end
    end
end

function DeviceDetector:OnDeactivate()
    self.kmHandler:Disconnect()
    self.padHandler:Disconnect()
end

return DeviceDetector
