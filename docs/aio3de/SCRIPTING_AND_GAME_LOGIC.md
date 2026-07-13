# Scripting and game logic

How to make things *happen* in your game: the two scripting systems (Lua and
Script Canvas), the event model they share, copy-paste recipes for the most
common gameplay tasks, and a map of every sample script that ships in this
repo.

New to the engine? Do [`QUICKSTART.md`](QUICKSTART.md) and
[`GETTING_STARTED_TUTORIAL.md`](GETTING_STARTED_TUTORIAL.md) first.

---

## 1. The mental model: entities, components, and buses

- An **entity** is an ID. **Components** give it data and behavior.
- Components (and scripts) talk to each other through **EBuses** — named event
  buses addressed by entity ID. You never hold a pointer to another component;
  you send a message to a bus address:
  - *Request buses* — call into a component:
    `TransformBus.Event.SetWorldTranslation(entityId, pos)`.
  - *Notification buses* — get called back: connect a handler and implement
    the named functions (`OnTick`, `OnPressed`, `OnCollisionBegin`, ...).
- Both Lua and Script Canvas expose the **same** buses (everything reflected
  to the BehaviorContext), so anything you can do in one you can do in the
  other. Recipes below show Lua; each has a Script Canvas equivalent node.

## 2. Choosing: Script Canvas vs Lua

| | Script Canvas | Lua |
|---|---|---|
| Style | visual node graph | code |
| Editor | **Tools → Script Canvas** | any text editor + **Script Canvas → hot-reload** on save |
| Component | **Script Canvas** component (`.scriptcanvas` asset) | **Lua Script** component (`.lua` asset) |
| Best for | designers, prototyping, wiring events | anything nontrivial, reusable components, source control diffs |

Both hot-reload: save the file/graph while the Editor runs and the change is
picked up.

## 3. Lua in five minutes

A Lua script component is a table with lifecycle methods. Create
`Scripts/hello.lua` anywhere under your project folder (the Asset Processor
picks it up), add a **Lua Script** component to an entity, and assign it.

```lua
local Hello =
{
    -- Properties appear in the Entity Inspector, per-entity editable.
    Properties =
    {
        Speed = { default = 5.0, suffix = " m/s", description = "Units per second" },
        Target = { default = EntityId() },   -- drag another entity in
    },
}

function Hello:OnActivate()
    -- called when the entity activates (game mode start / spawn)
    Debug.Log("Hello from " .. tostring(self.entityId))
    self.tickHandler = TickBus.Connect(self)   -- get OnTick every frame
end

function Hello:OnTick(deltaTime, timePoint)
    local pos = TransformBus.Event.GetWorldTranslation(self.entityId)
    pos.z = pos.z + self.Properties.Speed * deltaTime
    TransformBus.Event.SetWorldTranslation(self.entityId, pos)
end

function Hello:OnDeactivate()
    if self.tickHandler then self.tickHandler:Disconnect() end
end

return Hello
```

Rules of thumb:

- `self.entityId` is the entity the script is on.
- Always `Disconnect()` in `OnDeactivate` whatever you `Connect()`ed.
- `Debug.Log(...)` prints to the Editor Console.
- Math types are reflected: `Vector3(x,y,z)`, `Quaternion`, `Transform`,
  `Color`, `Aabb` — with the same methods as C++ (`pos:GetNormalized()`,
  `Quaternion.CreateRotationZ(angle)`, ...).

## 4. Input

Input flows: **`.inputbindings` asset** (maps raw keys/gamepad/mouse to named
events) → **Input component** on the entity → your script.

1. **Tools → Asset Editor → Input Bindings**: create `player.inputbindings`,
   add input event groups, e.g. `MoveForward` bound to keyboard `W` (use
   *Event value multiplier* `-1` on `S` for the same axis backwards).
2. Add an **Input** component to the entity, assign the asset.
3. Handle the events (requires the **StartingPointInput** gem, enabled by
   default in new projects):

```lua
function PlayerInput:OnActivate()
    self.forwardHandler = InputEventNotificationBus.Connect(
        self, InputEventNotificationId("MoveForward"))
end

-- called with the bound value (key: 1/0, gamepad stick: -1..1)
function PlayerInput:OnPressed(value)  end
function PlayerInput:OnHeld(value)     end
function PlayerInput:OnReleased(value) end
```

To handle several events in one script, connect multiple handlers with
distinct tables, or use the ready-made forwarding scripts in
`Gems/StartingPointInput/Assets/Scripts/Input/` (`pressed.lua`, `held.lua`,
`released.lua`) which re-broadcast input as `GameplayNotificationBus` events —
see `Gems/StartingPointMovement/Assets/Scripts/Components/MoveEntity.lua` for
the consuming side.

## 5. Recipes

### Recipe: WASD movement

`.inputbindings`: `MoveForward` (W=1, S=-1), `MoveRight` (D=1, A=-1). Then:

```lua
local PlayerMove =
{
    Properties = { Speed = { default = 8.0 } },
}

function PlayerMove:OnActivate()
    self.fwd, self.right = 0.0, 0.0
    self.fwdHandler = InputEventNotificationBus.Connect(self, InputEventNotificationId("MoveForward"))
    -- one handler table per event; route both to self via a proxy
    self.rightProxy = {
        OnPressed  = function(_, v) self.right = v end,
        OnHeld     = function(_, v) self.right = v end,
        OnReleased = function(_, v) self.right = 0.0 end,
    }
    self.rightHandler = InputEventNotificationBus.Connect(self.rightProxy, InputEventNotificationId("MoveRight"))
    self.tickHandler = TickBus.Connect(self)
end

function PlayerMove:OnPressed(v)  self.fwd = v end
function PlayerMove:OnHeld(v)     self.fwd = v end
function PlayerMove:OnReleased(v) self.fwd = 0.0 end

function PlayerMove:OnTick(dt, time)
    local tm = TransformBus.Event.GetWorldTM(self.entityId)
    local move = tm:GetBasisY() * self.fwd + tm:GetBasisX() * self.right
    if move:GetLengthSq() > 0.0 then
        local pos = tm:GetTranslation() + move:GetNormalized() * self.Properties.Speed * dt
        TransformBus.Event.SetWorldTranslation(self.entityId, pos)
    end
end

function PlayerMove:OnDeactivate()
    self.tickHandler:Disconnect()
    self.fwdHandler:Disconnect()
    self.rightHandler:Disconnect()
end

return PlayerMove
```

For a physics-driven character use a **PhysX Character Controller** component
and drive it with `CharacterControllerRequestBus.Event.AddVelocityForTick`
instead of setting the transform. Don't set the TransformComponent of an
entity with a **dynamic rigid body** — PhysX owns it (that's what the
"Transform ... set manually" warning means); use
`RigidBodyRequestBus.Event.SetLinearVelocity` / `ApplyLinearImpulse`.

### Recipe: spawning prefabs at runtime

```lua
local Spawner =
{
    Properties =
    {
        Prefab = { default = SpawnableScriptAssetRef(), description = "Prefab to spawn" },
        Offset = { default = Vector3(0, 0, 1) },
    },
}

function Spawner:OnActivate()
    self.mediator = SpawnableScriptMediator()
    self.ticket = self.mediator:CreateSpawnTicket(self.Properties.Prefab)
    local pos = TransformBus.Event.GetWorldTranslation(self.entityId) + self.Properties.Offset
    self.mediator:SpawnAndParentAndTransform(self.ticket, self.entityId,
        self.Properties.Offset, Vector3(0, 0, 0) --[[rotation]], 1.0 --[[scale]])
end

function Spawner:OnDeactivate()
    if self.mediator and self.ticket then
        self.mediator:Despawn(self.ticket)
    end
end

return Spawner
```

Keep the **ticket** alive as long as the spawned instance should exist —
letting it garbage-collect despawns the instance. Working examples:
`AutomatedTesting/LuaScripts/Spawnables/` (spawn, despawn, multiple spawns
from one ticket, nested spawners).

### Recipe: triggers (enter/exit a volume)

Setup: entity with a **PhysX Collider** ticked as **Trigger** + a **PhysX
Static Rigid Body**. In **Script Canvas** use the *On Trigger Enter* / *On
Trigger Exit* nodes — simplest path. In Lua, trigger/collision notifications
are AZ::Events on the simulated body (see the reflection in
`Code/Framework/AzFramework/AzFramework/Physics/Common/PhysicsSimulatedBody.cpp`):

```lua
function Door:OnActivate()
    -- SimulatedBody.GetOnTriggerEnterEvent(entityId) returns an AZ::Event you connect a function to
    self.enterHandler = SimulatedBody.GetOnTriggerEnterEvent(self.entityId)
    if self.enterHandler then
        self.enterHandler:Connect(function(bodyHandle, triggerEvent)
            local other = triggerEvent.otherBody and triggerEvent.otherBody:GetEntityId()
            Debug.Log("Trigger entered by " .. tostring(other))
        end)
    end
end
```

Collision (non-trigger) contacts work the same way via
`SimulatedBody.GetOnCollisionBeginEvent` / `...Persist...` / `...End...`.

### Recipe: timers and delayed actions

No dedicated timer bus — accumulate on tick:

```lua
function Bomb:OnActivate()
    self.remaining = 3.0
    self.tickHandler = TickBus.Connect(self)
end

function Bomb:OnTick(dt, time)
    self.remaining = self.remaining - dt
    if self.remaining <= 0.0 then
        self.tickHandler:Disconnect()
        self.tickHandler = nil
        -- boom: despawn, spawn an effect, send an event...
    end
end
```

### Recipe: decoupled game events (Script Events)

For your own cross-entity/cross-script events (score changed, door opened),
define a **Script Event** asset (**Tools → Asset Editor → Script Events**) —
it becomes a first-class bus callable from both Lua and Script Canvas.
Runnable Lua examples: `Gems/ScriptEvents/Assets/Scripts/Example/`
(`ScriptEvents_Addressable.lua`, `ScriptEvents_Broadcast.lua`).

Lighter-weight alternative used by the StartingPoint gems:
`GameplayNotificationBus` with a `GameplayNotificationId(entityId, "EventName",
"typeuuid")` address — good for simple value-forwarding chains.

### Recipe: enabling/disabling and finding entities

```lua
-- deactivate / reactivate an entity (and its children's components)
GameEntityContextRequestBus.Broadcast.DeactivateGameEntity(self.Properties.Target)
GameEntityContextRequestBus.Broadcast.ActivateGameEntity(self.Properties.Target)

-- parent/child
local children = TransformBus.Event.GetChildren(self.entityId)
TransformBus.Event.SetParent(childId, self.entityId)

-- name lookup (prefer entity references in Properties over name lookups)
local name = GameEntityContextRequestBus.Broadcast.GetEntityName(self.entityId)
```

### Recipe: camera and rendering tweaks from script

```lua
-- switch active camera
CameraRequestBus.Event.MakeActiveView(self.Properties.CameraEntity)

-- change a material property / mesh visibility
RenderMeshComponentRequestBus.Event.SetVisibility(self.entityId, false)

-- move a light's intensity (Atom lights)
AreaLightRequestBus.Event.SetIntensity(self.entityId, 100.0)
```

## 6. Script Canvas quick tour

1. **Tools → Script Canvas** opens the graph editor.
2. Every recipe above exists as nodes: right-click the canvas → node palette →
   search the bus name (`Transform`, `Input`, `On Trigger Enter`, `Spawn`).
3. A graph starts from event nodes (**On Graph Start**, **Input Handler**,
   **Tick**), flows left-to-right through logic/math nodes.
4. Save as `.scriptcanvas`, add a **Script Canvas** component to an entity,
   assign the graph. Graph **Variables** can be exposed to the component
   (tick *Instance editable*) — same idea as Lua `Properties`.

## 7. Sample scripts that ship in this repo

| Path | What it shows |
|---|---|
| `Gems/StartingPointInput/Assets/Scripts/Input/` | input event → gameplay event forwarding (`pressed/held/released.lua`), combining inputs into vectors (`vectorized_combination.lua`) |
| `Gems/StartingPointMovement/Assets/Scripts/Components/` | velocity movement (`MoveEntity.lua`), rotation (`RotateEntity.lua`), look-at (`EntityLookAt.lua`), impulses (`AddPhysicsImpulse.lua`) |
| `Gems/ScriptEvents/Assets/Scripts/Example/` | defining and using Script Events from Lua |
| `AutomatedTesting/LuaScripts/Spawnables/` | runtime prefab spawning/despawning patterns |
| `Gems/ScriptedEntityTweener/Assets/Scripts/` | tweening/animation of any component property from Lua |
| `Gems/AIBackbone/Editor/Scripts/` | (fork) Python editor-side AI model builder |

## 8. Debugging scripts

- `Debug.Log`, `Debug.Warning`, `Debug.Error` → Editor Console.
- Lua errors show in the Console with a stack trace; the script keeps its
  last-good version until the file parses again.
- Script Canvas has live **graph debugging**: enable *Debugging* on the graph
  and watch execution highlight nodes in game mode.
- `DebugDraw` gem: `DebugDrawRequestBus.Broadcast.DrawSphereAtLocation(pos,
  radius, Color(1,0,0,1), duration)` and friends for in-world visualizations.

## 9. Further reading

- Upstream Lua reference: <https://o3de.org/docs/user-guide/scripting/lua/>
- Upstream Script Canvas: <https://o3de.org/docs/user-guide/scripting/script-canvas/>
- Script Events: <https://o3de.org/docs/user-guide/scripting/script-events/>
- Input: <https://o3de.org/docs/user-guide/interactivity/input/>
- Prefab spawning API: <https://o3de.org/docs/user-guide/interactivity/prefabs/spawn-a-prefab/>
