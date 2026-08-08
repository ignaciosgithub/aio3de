-- 
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- For complete copyright and license terms please see the LICENSE at the root of this distribution.
-- 
-- SPDX-License-Identifier: Apache-2.0 OR MIT
-- 

-- ArenaShooter: first/third person player controller.
-- Requires on the same entity: PhysX Character Controller + PhysX Character Gameplay,
-- Input component with arenashooter.inputbindings, and a child entity with the Camera
-- component assigned to CameraEntity.
local PlayerController =
{
    Properties =
    {
        MoveSpeed = { default = 8.0, suffix = " m/s" },
        LookSensitivity = { default = 0.15, description = "Degrees per mouse count / stick unit" },
        JumpSpeed = { default = 6.0, suffix = " m/s" },
        MaxPitch = { default = 85.0, suffix = " deg" },
        DeviceDetectorEntity = { default = EntityId(), description = "Entity running DeviceDetector.lua (for per-device sensitivity); optional" },
        CameraEntity = { default = EntityId(), description = "Child entity carrying the Camera component (pitch is applied here)" },
    },
}

local function connectInput(self, eventName, target, field)
    local handler = {
        OnPressed = function(_, value) target[field] = value end,
        OnHeld = function(_, value) target[field] = value end,
        OnReleased = function(_, value) target[field] = 0.0 end,
    }
    return InputEventNotificationBus.Connect(handler, InputEventNotificationId(eventName))
end

function PlayerController:OnActivate()
    self.input = { fwd = 0.0, right = 0.0, lookX = 0.0, lookY = 0.0 }
    self.yaw = 0.0
    self.pitch = 0.0
    self.wantJump = false

    self.handlers = {
        connectInput(self, "MoveForward", self.input, "fwd"),
        connectInput(self, "MoveRight", self.input, "right"),
    }

    -- look events arrive as deltas; accumulate them and clear after use
    local lookXHandler = {
        OnPressed = function(_, v) self.input.lookX = self.input.lookX + v end,
        OnHeld = function(_, v) self.input.lookX = self.input.lookX + v end,
        OnReleased = function(_, v) end,
    }
    local lookYHandler = {
        OnPressed = function(_, v) self.input.lookY = self.input.lookY + v end,
        OnHeld = function(_, v) self.input.lookY = self.input.lookY + v end,
        OnReleased = function(_, v) end,
    }
    local jumpHandler = {
        OnPressed = function(_, v) self.wantJump = true end,
        OnHeld = function(_, v) end,
        OnReleased = function(_, v) end,
    }
    table.insert(self.handlers, InputEventNotificationBus.Connect(lookXHandler, InputEventNotificationId("LookX")))
    table.insert(self.handlers, InputEventNotificationBus.Connect(lookYHandler, InputEventNotificationId("LookY")))
    table.insert(self.handlers, InputEventNotificationBus.Connect(jumpHandler, InputEventNotificationId("Jump")))

    -- with the GameSettings gem, sensitivity comes from the user's saved
    -- settings (per active device, via DeviceDetector's broadcast)
    self.activeDevice = 1
    local deviceHandler = {
        OnEventBegin = function(_, v) self.activeDevice = v end,
        OnEventUpdating = function(_, v) self.activeDevice = v end,
        OnEventEnd = function(_, v) end,
    }
    local detector = self.Properties.DeviceDetectorEntity
    if not detector:IsValid() then
        detector = self.entityId
    end
    table.insert(self.handlers, GameplayNotificationBus.Connect(
        deviceHandler, GameplayNotificationId(detector, "ActiveInputDevice", "float")))

    self.tickHandler = TickBus.Connect(self)
end

function PlayerController:Sensitivity()
    local sens = self.Properties.LookSensitivity
    if GameSettingsRequestBus ~= nil then
        local setting = (self.activeDevice == 2) and "pad_sensitivity" or "mouse_sensitivity"
        sens = sens * GameSettingsRequestBus.Broadcast.GetValue(setting, 1.0)
    end
    return sens
end

function PlayerController:OnTick(deltaTime, timePoint)
    -- look: yaw the player, pitch the camera child
    local sensitivity = self:Sensitivity()
    self.yaw = self.yaw - self.input.lookX * sensitivity
    self.pitch = self.pitch - self.input.lookY * sensitivity
    local maxPitch = self.Properties.MaxPitch
    if self.pitch > maxPitch then self.pitch = maxPitch end
    if self.pitch < -maxPitch then self.pitch = -maxPitch end
    self.input.lookX = 0.0
    self.input.lookY = 0.0

    TransformBus.Event.SetLocalRotation(self.entityId, Vector3(0.0, 0.0, Math.DegToRad(self.yaw)))
    if self.Properties.CameraEntity:IsValid() then
        TransformBus.Event.SetLocalRotation(self.Properties.CameraEntity, Vector3(Math.DegToRad(self.pitch), 0.0, 0.0))
    end

    -- movement relative to facing
    local tm = TransformBus.Event.GetWorldTM(self.entityId)
    local move = tm:GetBasisY() * self.input.fwd + tm:GetBasisX() * self.input.right
    move.z = 0.0
    if move:GetLengthSq() > 1.0 then
        move = move:GetNormalized()
    end
    local velocity = move * self.Properties.MoveSpeed

    if self.wantJump then
        self.wantJump = false
        local grounded = CharacterGameplayRequestBus.Event.IsOnGround(self.entityId)
        if grounded then
            CharacterGameplayRequestBus.Event.SetFallingVelocity(self.entityId, Vector3(0.0, 0.0, self.Properties.JumpSpeed))
        end
    end

    CharacterControllerRequestBus.Event.AddVelocityForTick(self.entityId, velocity)

    -- broadcast our planar speed so animation/AI can react
    GameplayNotificationBus.Event.OnEventUpdating(
        GameplayNotificationId(self.entityId, "MoveSpeed", "float"), velocity:GetLength())
end

function PlayerController:OnDeactivate()
    if self.tickHandler then self.tickHandler:Disconnect() end
    for _, handler in ipairs(self.handlers or {}) do
        handler:Disconnect()
    end
end

return PlayerController
