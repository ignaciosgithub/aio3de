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

See `Examples/Mover.cs` for a complete sample.

### API (AIO3DE.Core)

- `ScriptComponent` — base class; lifecycle: `OnActivate()`, `OnUpdate(float deltaTime)`, `OnDeactivate()`; `Entity` field = the entity the script is on.
- `Entity` — transform: `Position`, `LocalPosition`, `RotationEuler` (degrees), `Rotation` (quaternion), `UniformScale`, `ForwardVector`/`RightVector`/`UpVector`, `Parent` (get/set); lifecycle: `Entity.Find(name)`, `Entity.Create(name)`, `Destroy()`, `IsActive`, `SetActive(bool)`; rigid body (needs a Rigid Body component): `LinearVelocity`, `AngularVelocity`, `ApplyImpulse`, `ApplyAngularImpulse`, `Mass`, `SetGravityEnabled`, `SetKinematic`.
- `Input` — `GetKey("W")` / `GetKey("Space")` / `GetKey("LShift")`..., `GetMouseButton(0/1/2)`, `MouseDelta`, `CursorPosition` (normalized), plus raw channels: `IsHeld("keyboard_key_alphanumeric_W")`, `GetValue("mouse_delta_x")` (any O3DE input channel name, including gamepads).
- `Physics` — `Raycast(origin, direction, maxDistance, out RaycastHit hit)` against the default physics scene; `RaycastHit` has `Position`, `Normal`, `Distance`, `Entity`.
- `Time` — `TimeSinceStart` (seconds since app start; per-frame delta comes via `OnUpdate`).
- `Debug` — `Log`, `LogWarning`, `LogError` (go to the engine console/log).
- `Vector3` — full float3 math: operators, `Dot`, `Cross`, `Normalized()`, `Distance`, `Lerp`, `Zero/One/Up/Forward/Right`.
- `Quaternion` — `Identity`, `FromAxisAngle(axis, degrees)`, multiplication, `Rotate(vector)`.

Note: Z is up, Y is forward (O3DE convention). Entities spawned with `Entity.Create` start empty
with just a transform; prefab/spawnable instantiation is a future binding.

## How it works

The gem hosts CoreCLR in-process via `hostfxr` (found through `DOTNET_ROOT` or the standard
install paths). The managed API (`AIO3DE.Core`) and your project's scripts are built with
`dotnet build` into `<project>/user/csharp/`. Script classes are instantiated by reflection and
driven through `[UnmanagedCallersOnly]` entry points; engine calls flow back through a native
function table.
