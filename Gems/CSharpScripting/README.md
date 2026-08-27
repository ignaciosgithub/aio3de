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

- `ScriptComponent` — base class; lifecycle: `OnActivate()`, `OnUpdate(float deltaTime)`, `OnDeactivate()`; `Entity` property = the entity the script is on.
- `Entity` — `Position` (world), `RotationEuler` (world, degrees), `UniformScale`, `Name`, `Entity.Find(name)`.
- `Debug` — `Log`, `LogWarning`, `LogError` (go to the engine console/log).
- `Vector3` — simple float3 with +, -, * and `Length()`.

More bindings (physics, raycasts, input, spawning) will arrive in later iterations.

## How it works

The gem hosts CoreCLR in-process via `hostfxr` (found through `DOTNET_ROOT` or the standard
install paths). The managed API (`AIO3DE.Core`) and your project's scripts are built with
`dotnet build` into `<project>/user/csharp/`. Script classes are instantiated by reflection and
driven through `[UnmanagedCallersOnly]` entry points; engine calls flow back through a native
function table.
