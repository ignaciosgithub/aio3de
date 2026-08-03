-- ArenaShooter: drives an EMotionFX anim graph from gameplay state.
-- Put on the character entity that has Actor + Anim Graph components.
-- Feeds these anim graph parameters (create them as float parameters in the
-- Animation Editor and use them in your state machine transitions):
--   "MoveSpeed" - planar speed in m/s (from PlayerController's MoveSpeed event)
--   "Shooting"  - 1.0 for a short window after each shot, else 0.0
local AnimationDriver =
{
    Properties =
    {
        SourceEntity = { default = EntityId(), description = "Entity emitting MoveSpeed events (the player root); this entity if unset" },
        ShootHoldTime = { default = 0.25, suffix = " s" },
    },
}

function AnimationDriver:OnActivate()
    self.shootTimer = 0.0

    local source = self.Properties.SourceEntity
    if not source:IsValid() then
        source = self.entityId
    end

    local speedHandler = {
        OnEventBegin = function(_, v) self:SetSpeed(v) end,
        OnEventUpdating = function(_, v) self:SetSpeed(v) end,
        OnEventEnd = function(_, v) end,
    }
    self.speedHandler = GameplayNotificationBus.Connect(
        speedHandler, GameplayNotificationId(source, "MoveSpeed", "float"))

    self.shootHandler = InputEventNotificationBus.Connect({
        OnPressed = function(_, v) self.shootTimer = self.Properties.ShootHoldTime end,
        OnHeld = function(_, v) self.shootTimer = self.Properties.ShootHoldTime end,
        OnReleased = function(_, v) end,
    }, InputEventNotificationId("Shoot"))

    self.tickHandler = TickBus.Connect(self)
end

function AnimationDriver:SetSpeed(speed)
    AnimGraphComponentRequestBus.Event.SetNamedParameterFloat(self.entityId, "MoveSpeed", speed)
end

function AnimationDriver:OnTick(deltaTime, timePoint)
    local shooting = 0.0
    if self.shootTimer > 0.0 then
        self.shootTimer = self.shootTimer - deltaTime
        shooting = 1.0
    end
    AnimGraphComponentRequestBus.Event.SetNamedParameterFloat(self.entityId, "Shooting", shooting)
end

function AnimationDriver:OnDeactivate()
    self.tickHandler:Disconnect()
    self.shootHandler:Disconnect()
    self.speedHandler:Disconnect()
end

return AnimationDriver
