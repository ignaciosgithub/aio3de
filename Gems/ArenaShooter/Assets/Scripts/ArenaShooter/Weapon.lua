-- ArenaShooter: hitscan weapon. Put on the player entity.
-- Fires a physics ray from the camera on the "Shoot" input event, applies damage
-- (via a "Damage" gameplay event on the hit entity) and a knockback impulse to
-- dynamic rigid bodies.
local Weapon =
{
    Properties =
    {
        CameraEntity = { default = EntityId(), description = "Entity to shoot from (usually the player camera)" },
        Range = { default = 200.0, suffix = " m" },
        Damage = { default = 25.0 },
        ImpactImpulse = { default = 250.0, suffix = " N s" },
        FireInterval = { default = 0.15, suffix = " s", description = "Minimum time between shots" },
        Slot = { default = 0, description = "Weapon slot for WeaponSwitcher.lua (0 = always active, no switching)" },
        FireSoundEntity = { default = EntityId(), description = "Optional entity with a MiniAudio Playback component to play on each shot (enable spatialization on it for 3D audio)" },
    },
}

function Weapon:OnActivate()
    self.cooldown = 0.0
    self.triggerHeld = false
    self.active = (self.Properties.Slot <= 0) -- slot 1 is enabled by the switcher's initial broadcast
    self.shootHandler = InputEventNotificationBus.Connect(self, InputEventNotificationId("Shoot"))
    self.tickHandler = TickBus.Connect(self)
    if self.Properties.Slot > 0 then
        self.slotHandler = GameplayNotificationBus.Connect(
            { OnEventBegin = function(_, slot) self.active = (slot == self.Properties.Slot) end },
            GameplayNotificationId(self.entityId, "WeaponSlot", "float"))
    end
end

function Weapon:OnPressed(value)
    self.triggerHeld = true
end

function Weapon:OnHeld(value)
    self.triggerHeld = true
end

function Weapon:OnReleased(value)
    self.triggerHeld = false
end

function Weapon:OnTick(deltaTime, timePoint)
    self.cooldown = self.cooldown - deltaTime
    if self.active and self.triggerHeld and self.cooldown <= 0.0 then
        self.cooldown = self.Properties.FireInterval
        self:Fire()
    end
end

function Weapon:Fire()
    local sourceEntity = self.Properties.CameraEntity
    if not sourceEntity:IsValid() then
        sourceEntity = self.entityId
    end
    local tm = TransformBus.Event.GetWorldTM(sourceEntity)
    local start = tm:GetTranslation()
    local direction = -tm:GetBasisZ() -- cameras look down -Z

    local physicsSystem = GetPhysicsSystem()
    local sceneHandle = physicsSystem:GetSceneHandle(DefaultPhysicsSceneName)
    local scene = physicsSystem:GetScene(sceneHandle)
    if scene == nil then
        return
    end

    if self.Properties.FireSoundEntity:IsValid() then
        MiniAudioPlaybackRequestBus.Event.Play(self.Properties.FireSoundEntity)
    end

    local request = SceneQueries.CreateRayCastRequest(start, direction, self.Properties.Range, "All")
    local hits = scene:QueryScene(request)

    for i = 1, #hits.HitArray do
        local hit = hits.HitArray[i]
        if hit.EntityId ~= self.entityId then
            -- damage: broadcast to the hit entity; Health.lua listens for this
            GameplayNotificationBus.Event.OnEventBegin(
                GameplayNotificationId(hit.EntityId, "Damage", "float"), self.Properties.Damage)

            -- knockback for dynamic rigid bodies
            RigidBodyRequestBus.Event.ApplyLinearImpulseAtWorldPoint(
                hit.EntityId, direction * self.Properties.ImpactImpulse, hit.Position)

            -- brief impact marker for debugging (requires the DebugDraw gem)
            DebugDrawRequestBus.Broadcast.DrawSphereAtLocation(hit.Position, 0.06, Color(1.0, 0.5, 0.0, 1.0), 0.25)
            break
        end
    end
end

function Weapon:OnDeactivate()
    self.tickHandler:Disconnect()
    self.shootHandler:Disconnect()
    if self.slotHandler then
        self.slotHandler:Disconnect()
    end
end

return Weapon
