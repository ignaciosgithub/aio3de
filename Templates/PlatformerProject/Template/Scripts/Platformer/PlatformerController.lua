--
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- For complete copyright and license terms please see the LICENSE at the root of this distribution.
--
-- SPDX-License-Identifier: Apache-2.0 OR MIT
--

local PlatformerController =
{
    Properties =
    {
        MoveSpeed = { default = 7.0 },
        JumpSpeed = { default = 7.0 },
        MaxJumps = { default = 2 },
        LookSensitivity = { default = 0.15 },
        MaxPitch = { default = 75.0 },
        Body = { default = EntityId() },
        CameraPivot = { default = EntityId() },
    },
}

local function connectInput(eventName, target, field, callbackTables)
    local callbackTable = {
        OnPressed = function(_, value) target[field] = value end,
        OnHeld = function(_, value) target[field] = value end,
        OnReleased = function(_, value) target[field] = 0.0 end,
    }
    table.insert(callbackTables, callbackTable)
    return InputEventNotificationBus.Connect(callbackTable, InputEventNotificationId(eventName))
end

local function worldPosition(entityId)
    return TransformBus.Event.GetWorldTranslation(entityId)
end

local function isMovingPlatform(entityId)
    local name = GameEntityContextRequestBus.Broadcast.GetEntityName(entityId)
    return string.find(name, "Moving Platform", 1, true) == 1
        or string.find(name, "Vertical Lift", 1, true) == 1
end

local function atan2(y, x)
    if math.atan2 then
        return math.atan2(y, x)
    end
    return math.atan(y, x)
end

local function findGroundHit(entityId)
    local physics = GetPhysicsSystem()
    local scene = physics:GetScene(physics:GetSceneHandle(DefaultPhysicsSceneName))
    if scene == nil then
        return nil
    end

    local start = worldPosition(entityId) + Vector3(0.0, 0.0, 0.2)
    local request = SceneQueries.CreateRayCastRequest(start, Vector3(0.0, 0.0, -1.0), 0.5, "All")
    local hits = scene:QueryScene(request)
    for index = 1, hits.HitArray:Size() do
        local hit = hits.HitArray[index]
        if hit.EntityId ~= entityId then
            return hit
        end
    end
    return nil
end

function PlatformerController:OnActivate()
    self.input = { forward = 0.0, right = 0.0, lookX = 0.0, lookY = 0.0 }
    self.yaw = 0.0
    self.pitch = 0.0
    self.jumps = 0
    self.wantJump = false
    self.pendingGroundJump = false
    self.lastPlatformName = nil
    self.lastPlatformPosition = nil
    self.stompTimer = 0.0
    self.playerBeaconTimer = 0.0

    self.callbackTables = {}
    self.handlers = {
        connectInput("MoveForward", self.input, "forward", self.callbackTables),
        connectInput("MoveRight", self.input, "right", self.callbackTables),
    }
    local lookXHandler = {
            OnPressed = function(_, value) self.input.lookX = self.input.lookX + value end,
            OnHeld = function(_, value) self.input.lookX = self.input.lookX + value end,
            OnReleased = function() end,
    }
    table.insert(self.callbackTables, lookXHandler)
    table.insert(self.handlers, InputEventNotificationBus.Connect(
        lookXHandler, InputEventNotificationId("LookX")))
    local lookYHandler = {
            OnPressed = function(_, value) self.input.lookY = self.input.lookY + value end,
            OnHeld = function(_, value) self.input.lookY = self.input.lookY + value end,
            OnReleased = function() end,
    }
    table.insert(self.callbackTables, lookYHandler)
    table.insert(self.handlers, InputEventNotificationBus.Connect(
        lookYHandler, InputEventNotificationId("LookY")))
    local jumpHandler = {
            OnPressed = function() self.wantJump = true end,
            OnHeld = function() end,
            OnReleased = function() end,
    }
    table.insert(self.callbackTables, jumpHandler)
    table.insert(self.handlers, InputEventNotificationBus.Connect(
        jumpHandler, InputEventNotificationId("Jump")))

    local deviceHandler = {
        OnEventBegin = function(_, value) self.activeDevice = value end,
        OnEventUpdating = function(_, value) self.activeDevice = value end,
        OnEventEnd = function() end,
    }
    table.insert(self.callbackTables, deviceHandler)
    table.insert(self.handlers, GameplayNotificationBus.Connect(
        deviceHandler, GameplayNotificationId(self.entityId, "ActiveInputDevice", "float")))
    local stompedHandler = { OnEventBegin = function() self:OnStomped() end }
    table.insert(self.callbackTables, stompedHandler)
    table.insert(self.handlers, GameplayNotificationBus.Connect(
        stompedHandler,
        GameplayNotificationId(self.entityId, "Stomped", "float")))

    self.activeDevice = 1.0
    self.tickHandler = TickBus.Connect(self)
end

function PlatformerController:Sensitivity()
    local setting = (self.activeDevice == 2.0) and "pad_sensitivity" or "mouse_sensitivity"
    return self.Properties.LookSensitivity * GameSettingsRequestBus.Broadcast.GetValue(setting, 1.0)
end

function PlatformerController:OnTick(deltaTime)
    self.playerBeaconTimer = self.playerBeaconTimer - deltaTime
    if self.playerBeaconTimer <= 0.0 then
        GameplayNotificationBus.Event.OnEventBegin(
            GameplayNotificationId(EntityId(), "PlatformerPlayer", "EntityId"), self.entityId)
        self.playerBeaconTimer = 0.2
    end

    self.yaw = self.yaw - self.input.lookX * self:Sensitivity()
    self.pitch = math.max(-self.Properties.MaxPitch,
        math.min(self.Properties.MaxPitch, self.pitch - self.input.lookY * self:Sensitivity()))
    self.input.lookX = 0.0
    self.input.lookY = 0.0

    local pivot = self.Properties.CameraPivot
    if pivot:IsValid() then
        TransformBus.Event.SetLocalRotation(
            pivot, Vector3(Math.DegToRad(self.pitch), 0.0, Math.DegToRad(self.yaw)))
    end

    local move = Vector3(0.0, 0.0, 0.0)
    if pivot:IsValid() then
        local cameraTransform = TransformBus.Event.GetWorldTM(pivot)
        move = cameraTransform:GetBasisY() * self.input.forward
            + cameraTransform:GetBasisX() * self.input.right
        move.z = 0.0
    end
    if move:GetLengthSq() > 1.0 then
        move = move:GetNormalized()
    end

    if self.Properties.Body:IsValid() and move:GetLengthSq() > 0.0001 then
        local yaw = atan2(-move.x, move.y)
        TransformBus.Event.SetLocalRotation(self.Properties.Body, Vector3(0.0, 0.0, yaw))
    end

    local grounded = CharacterGameplayRequestBus.Event.IsOnGround(self.entityId)
    if grounded and not self.pendingGroundJump then
        self.jumps = 0
    end

    local jumpVelocity = Vector3(0.0, 0.0, 0.0)
    if self.pendingGroundJump then
        if grounded then
            jumpVelocity = Vector3(0.0, 0.0, self.Properties.JumpSpeed)
        else
            CharacterGameplayRequestBus.Event.SetFallingVelocity(
                self.entityId, Vector3(0.0, 0.0, self.Properties.JumpSpeed))
            self.pendingGroundJump = false
        end
    end

    if self.wantJump then
        self.wantJump = false
        if self.jumps < self.Properties.MaxJumps then
            if grounded then
                self.pendingGroundJump = true
                jumpVelocity = Vector3(0.0, 0.0, self.Properties.JumpSpeed)
            else
                CharacterGameplayRequestBus.Event.SetFallingVelocity(
                    self.entityId, Vector3(0.0, 0.0, self.Properties.JumpSpeed))
            end
            self.jumps = self.jumps + 1
        end
    end

    local carryVelocity = Vector3(0.0, 0.0, 0.0)
    local hit = findGroundHit(self.entityId)
    if hit ~= nil and isMovingPlatform(hit.EntityId) then
        local platformName = GameEntityContextRequestBus.Broadcast.GetEntityName(hit.EntityId)
        local platformPosition = worldPosition(hit.EntityId)
        if self.lastPlatformName == platformName and self.lastPlatformPosition ~= nil and deltaTime > 0.0 then
            carryVelocity = (platformPosition - self.lastPlatformPosition) / deltaTime
        end
        self.lastPlatformName = platformName
        self.lastPlatformPosition = platformPosition
    else
        self.lastPlatformName = nil
        self.lastPlatformPosition = nil
    end

    self.stompTimer = math.max(0.0, self.stompTimer - deltaTime)
    local fallingVelocity = CharacterGameplayRequestBus.Event.GetFallingVelocity(self.entityId)
    if hit ~= nil and self.stompTimer <= 0.0 and fallingVelocity.z < 0.0 then
        GameplayNotificationBus.Event.OnEventBegin(
            GameplayNotificationId(hit.EntityId, "Stomp", "float"), 1.0)
        self.stompTimer = 0.25
    end

    local velocity = move * self.Properties.MoveSpeed + carryVelocity + jumpVelocity
    CharacterControllerRequestBus.Event.AddVelocityForTick(self.entityId, velocity)
end

function PlatformerController:OnStomped()
    CharacterGameplayRequestBus.Event.SetFallingVelocity(
        self.entityId, Vector3(0.0, 0.0, 8.0))
    self.jumps = 1
    self.stompTimer = 0.25
end

function PlatformerController:OnDeactivate()
    if self.tickHandler then self.tickHandler:Disconnect() end
    for _, handler in ipairs(self.handlers or {}) do
        handler:Disconnect()
    end
    self.callbackTables = nil
end

return PlatformerController
