# CSharpScripting Gem

Unity-style C# scripting for AIO3DE, hosted on .NET 8 (CoreCLR). Works on Linux and Windows.

## Requirements

- The .NET 8 SDK (`dotnet` CLI). Install from https://dotnet.microsoft.com/download
  (Ubuntu: `sudo apt install dotnet-sdk-8.0`). If installed to a custom location, set `DOTNET_ROOT`.

## Setup

1. Enable the gem on your project:
   `scripts/o3de.sh enable-gem -gn CSharpScripting -pp <project path>` (or via the hub Gems tab),
   then reconfigure + rebuild the project.
2. Create a `Scripts` folder in your project root and add `.cs` files.
3. Add a **C# Script** component (Add Component → Scripting) to an entity and set **Class name**
   to your class (namespace-qualified if declared in a namespace).

Scripts are compiled automatically with `dotnet build` the first time one is needed; build errors
appear in the console. Use the component's **Rebuild scripts** button (or the `csharp_rebuild`
console command) to recompile after editing. Newly compiled code applies to script instances
created afterwards (e.g. the next time you enter game mode).

## Writing scripts

Derive from `AIO3DE.ScriptComponent`:

```csharp
using AIO3DE;

public class Mover : ScriptComponent
{
    public override void OnActivate() { Debug.Log($"hello from {Entity.Name}"); }

    public override void OnUpdate(float deltaTime)
    {
        Vector3 p = Entity.Position;
        p.Z += deltaTime;          // rise 1 unit/second
        Entity.Position = p;
    }

    public override void OnDeactivate() { }
}
```

See the `Examples/` folder for samples:

- `Mover.cs` — minimal movement + input.
- `FpsController.cs` — full FPS character (mouse/gamepad look, jump, drag, shooting).
- `Spawner.cs` — prefab spawning, tags, collision callbacks.
- `PhysicsPusher.cs` — `[RequireComponent("RigidBody")]`-gated physics: impulses, torque, toggling gravity/kinematic, reading/zeroing velocity and mass.
- `FollowTarget.cs` — another entity as an Inspector variable (`public Entity Target`) with a name-lookup fallback.
- `LifetimeManager.cs` — creating empty entities, spawning/despawning prefabs, destroying another entity (Inspector-assigned `Victim`), and self-destructing.
- `RaycastZapper.cs` — raycasts plus per-hit `HasComponent("RigidBody")` checks before pushing other entities.
- `ComponentTweaker.cs` — generic component access: listing components/properties, reading and writing any reflected property (`GetComponent("RigidBody").Set("Linear damping", ...)`), `AddComponent`/`RemoveComponent` at runtime, and calling another entity's script via `GetScript<T>()`.

### Inspector fields (Unity-style)

Public fields of supported types are shown in the Inspector on the C# Script component, with the
script's initializers as defaults — edit them per entity, no code change needed:

```csharp
public class Enemy : ScriptComponent
{
    public float MoveSpeed = 4.0f;        // shows as "Move Speed"
    public int Health = 100;
    public bool Aggressive = true;
    public string DisplayName = "Grunt";
    public Vector3 PatrolOffset = new Vector3(0, 5, 0);

    [SerializeField] private float _attackRange = 2.0f;  // private but exposed
    [HideInInspector] public float Internal;             // public but hidden
}
```

Supported types: `float`, `int`, `bool`, `string`, `Vector3`, `Entity`. `Entity` fields show an
entity picker — pick (or drag from the Outliner) any entity in the level to reference it from the
script, then guard with `if (target.IsValid)`. Values are saved with the level,
applied to the script instance right before `OnActivate`, and re-read from the class (keeping
your edits for fields that still exist) when you change the class name or press Rebuild scripts.

### Required components

Gate a script on a component with `[RequireComponent("...")]` (repeatable) or query at runtime
with `Entity.HasComponent("...")`. Matching is a case-insensitive substring of the component's
type name, so `"RigidBody"` matches the PhysX Rigid Body component, and `"Camera"`, `"Mesh"`,
`"Tag"`, `"BoxShape"` etc. work the same way:

```csharp
[RequireComponent("RigidBody")]
public class PhysicsPusher : ScriptComponent { ... }
```

If a required component is missing, the script logs a warning at activation and receives no
lifecycle callbacks (no `OnActivate`/`OnUpdate`/collision/trigger calls) — it does not add the
component for you.

### Generic component access

Any component's serialized properties can be read and written by name through a `Component`
handle, and components can be added/removed at runtime:

```csharp
Component body = Entity.GetComponent("RigidBody");   // partial, case-insensitive type name
if (body.IsValid)
{
    foreach (string p in body.Properties) Debug.Log(p);   // "RigidBodyConfiguration/Mass|float", ...
    float mass = body.GetFloat("Mass");                    // short name: matched at any depth
    body.Set("Linear damping", 0.5f, reactivate: true);    // spaces/underscores/case ignored
    body.Set("RigidBodyConfiguration/Gravity Enabled", false, reactivate: true);
}

Entity.GetComponent("BoxShape").Set("Dimensions", new Vector3(2, 2, 2), reactivate: true);
Entity.AddComponent("BoxShape");     // applied next frame; false if no such component type exists
Entity.RemoveComponent("Tag");       // applied next frame; false when the entity has none
string[] names = Entity.Components;  // every component type name on the entity

// Script-to-script calls (Unity's GetComponent<MyScript>()):
PhysicsPusher? pusher = other.GetScript<PhysicsPusher>();
if (pusher != null) pusher.ImpulseStrength = 10;
```

Getters: `GetFloat`, `GetInt`, `GetBool`, `GetString`, `GetVector3`, `GetQuaternion`, `GetEntity`,
`GetFloats` (vector2/vector4/color components), `GetRaw`/`TypeOf`/`Has`. `Set(...)` overloads
take `float`, `int`, `bool`, `string`, `Vector3`, `Quaternion`, `Entity`, `float[]`, or a raw
string. Every call is safe on a missing entity/component/property: getters return the fallback
(or `null`) and setters return `false`.

Supported property types: `float`/`double`, integers, `bool`, `string`, `Vector2/3/4`,
`Quaternion`, `Color`, `EntityId`. Asset references, enums, containers and nested objects are not
exposed (they do not show up in `Properties`); use the typed API or a Lua/EBus path for those.

Lifecycle notes: most components read their configuration when the entity activates, so pass
`reactivate: true` to make a written value take effect - the entity is deactivated and
reactivated at the start of the next frame, which also re-runs `OnDeactivate`/`OnActivate` on
its scripts. `AddComponent`/`RemoveComponent` do the same reactivation. Properties written
without `reactivate` change the serialized value immediately but the component may keep
using its old runtime state until the next activation. In the Editor, runtime changes made
in game mode are discarded on exit like every other game-mode change.

Lua scripts get the same via the standard `Properties` table on the Script component:

```lua
local enemy = {
    Properties = {
        MoveSpeed = 4.0,
        Health = { default = 100, min = 0, description = "Hit points" },
    },
}
function enemy:OnActivate()
    Debug.Log("speed " .. tostring(self.Properties.MoveSpeed))
end
return enemy
```

### API (AIO3DE.Core)

- `ScriptComponent` — base class; lifecycle: `OnActivate()`, `OnUpdate(float deltaTime)`, `OnDeactivate()`; collision callbacks (needs a PhysX collider/rigid body on the entity): `OnCollisionEnter(Collision)`, `OnCollisionExit(Entity other)`; trigger callbacks (fire on both the trigger and the entering body, needs a trigger collider on one of them): `OnTriggerEnter(Entity other)`, `OnTriggerExit(Entity other)`; `Entity` field = the entity the script is on.
- `Component` — reflection-backed handle from `Entity.GetComponent`: `IsValid`, `Properties`, `Has`, `TypeOf`, `GetRaw`, `GetFloat/GetInt/GetBool/GetString/GetVector3/GetQuaternion/GetEntity/GetFloats`, `Set(property, value, reactivate)` overloads, `SetRaw`.
- `Collision` — payload for `OnCollisionEnter`: `Other` entity, first contact `Position`, `Normal`, and `Impulse` magnitude.
- `Prefab` — `Prefab.Spawn("path/to/thing.spawnable", position)` instantiates a processed prefab (spawnable) at runtime. Spawning is asynchronous: the returned `PrefabInstance.RootEntity` becomes valid once spawning completes (usually the next frame). Keep the `PrefabInstance` and call `Despawn()` to remove all its entities.
- `Entity` — transform: `Position`, `LocalPosition`, `RotationEuler` (degrees), `Rotation` (quaternion), `UniformScale`, `ForwardVector`/`RightVector`/`UpVector`, `Parent` (get/set); lifecycle: `Entity.Find(name)`, `Entity.Create(name)`, `Destroy()`, `IsActive`, `SetActive(bool)`; component queries: `HasComponent("RigidBody")` (case-insensitive substring of the component type name), `Components`, `GetComponent(name)` -> `Component`, `AddComponent(name)`, `RemoveComponent(name)`; scripts: `GetScript<T>()`, `GetScripts()`; rigid body (needs a Rigid Body component): `LinearVelocity`, `AngularVelocity`, `ApplyImpulse`, `ApplyAngularImpulse`, `Mass`, `SetGravityEnabled`, `SetKinematic`; tags (needs a Tag component): `HasTag`, `AddTag`, `RemoveTag`, `Entity.FindByTag(tag)`, `Entity.FindAllByTag(tag)`.
- `Input` — `GetKey("W")` / `GetKey("Space")` / `GetKey("LShift")`..., `GetMouseButton(0/1/2)`, `MouseDelta`, `CursorPosition` (normalized), plus raw channels: `IsHeld("keyboard_key_alphanumeric_W")`, `GetValue("mouse_delta_x")` (any O3DE input channel name, including gamepads).
- `Physics` — `Raycast(origin, direction, maxDistance, out RaycastHit hit)` against the default physics scene; `RaycastHit` has `Position`, `Normal`, `Distance`, `Entity`.
- `Time` — `TimeSinceStart` (seconds since app start; per-frame delta comes via `OnUpdate`).
- `Debug` — `Log`, `LogWarning`, `LogError` (go to the engine console/log).
- `Vector3` — full float3 math: operators, `Dot`, `Cross`, `Normalized()`, `Distance`, `Lerp`, `Zero/One/Up/Forward/Right`.
- `Quaternion` — `Identity`, `FromAxisAngle(axis, degrees)`, multiplication, `Rotate(vector)`.

Note: Z is up, Y is forward (O3DE convention). Entities spawned with `Entity.Create` start empty
with just a transform; use `Prefab.Spawn` to instantiate authored prefabs (the Asset Processor
turns each `.prefab` into a runtime `.spawnable` — pass its cache-relative path, e.g.
`"prefabs/enemy.spawnable"`).

### Hot reload

Scripts load into a collectible `AssemblyLoadContext`. `csharp_rebuild` (or the component's
Rebuild button) recompiles and reloads: existing script instances are deactivated and the old
assembly is unloaded, so repeated rebuilds do not leak memory. New instances (e.g. next game-mode
entry) use the new code.

## How it works

The gem hosts CoreCLR in-process via `hostfxr` (found through `DOTNET_ROOT` or the standard
install paths). The managed API (`AIO3DE.Core`) and your project's scripts are built with
`dotnet build` into `<project>/user/csharp/`. Script classes are instantiated by reflection and
driven through `[UnmanagedCallersOnly]` entry points; engine calls flow back through a native
function table.
