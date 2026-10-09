# VoxelNav — voxelized 3D pathfinding

`VoxelNav` bakes the physics colliders of a level into a 3D voxel grid and answers path queries with a
3D A* search, for **walking** agents (grounded, step-up/drop limits, radius and height clearance — fully 3D,
so overhangs, bridges and multi-storey layouts work) and **flying** agents (free 26-neighbour movement
with sphere clearance). Paths are string-pulled with voxel line-of-sight so agents only turn where they must.

## Setup

1. Enable the gem for your project (the `CSharpScripting` gem already depends on it):
   `scripts/o3de.sh enable-gem -gn VoxelNav -pp <project>` (Windows: `scripts\o3de.bat ...`), then
   rebuild (new C++ gem).
2. Add a **Voxel Nav Volume** component (category *AI*) to an entity. The volume is centred on the entity
   and `Size` wide; a cyan wire box shows it in the Editor.
3. Set `Voxel size` (0.25–0.5 m is typical), pick the agent `Mode` and its radius / height / step limits.
4. Enter game mode. The volume bakes on the first frame (or over several frames when `Bake budget` > 0)
   using the colliders present in the PhysX scene, then prints a one-line summary to the console.

Everything with a static PhysX collider counts as solid (terrain, meshes with colliders, primitive shapes).
Dynamic rigid bodies are ignored unless `Include dynamic bodies` is on; trigger volumes currently count as
solid, so keep them outside nav volumes or rebuild after disabling them. Call `Rebuild` after the level
geometry changes.

## Querying

Lua / Script Canvas (global bus, routed to the volume containing the start point):

```lua
if VoxelNavRequestBus.Broadcast.IsReady() then
    local path = VoxelNavRequestBus.Broadcast.FindPath(startPos, goalPos)   -- vector of Vector3
    local raw  = VoxelNavRequestBus.Broadcast.FindRawPath(startPos, goalPos) -- every voxel, unsmoothed
    local ok   = VoxelNavRequestBus.Broadcast.IsNavigable(pos)
    local near = VoxelNavRequestBus.Broadcast.GetNearestNavigable(pos, 3.0)
    VoxelNavRequestBus.Broadcast.Rebuild()
end
-- Per-volume: VoxelNavVolumeRequestBus.Event.FindPath(volumeEntityId, startPos, goalPos), GetBounds, ...
```

C# (`AIO3DE.Pathfinding`):

```csharp
if (Pathfinding.IsReady)
{
    Vector3[] path = Pathfinding.FindPath(Entity.Position, Target.Position);
    bool ok = Pathfinding.IsNavigable(somewhere);
    Pathfinding.TryGetNearestNavigable(somewhere, 3.0f, out Vector3 nearest);
}
```

Walk-mode points are **feet positions** (bottom-centre of the voxel); Fly-mode points are voxel centres.
Start and goal are snapped to the nearest navigable voxel within `Snap distance`. An empty result means
no path (or the volume has not finished baking); the reason is printed with `AZ_TracePrintf` in the
`VoxelNav` window.

Examples: `Gems/VoxelNav/Examples/PathFollower.lua` and `Gems/CSharpScripting/Examples/PathFollower.cs`
move an entity to a target entity along the path, re-planning every half second.

## How it works

- **Grid** — bit-packed occupancy (`VoxelGrid`), bounds rounded up to whole voxels, capped at 2^27 voxels.
- **Bake** — hierarchical PhysX box-overlap queries (8³ block → 2³ sub-block → voxel) so empty space costs
  one query; query boxes are shrunk 4 % so geometry that merely touches a voxel face is not solid.
- **Clearance** (`NavData`) — solids are dilated by the agent radius (disc per layer for Walk, sphere for
  Fly). A Walk voxel is navigable when it is free, has an unblocked body column of `Height` above it and a
  raw solid voxel directly below (the floor). A Fly voxel is navigable when it is not blocked.
- **Search** (`VoxelPathfinder`) — A* with Euclidean costs. Walk: 8 horizontal neighbours each tried at the
  heights allowed by `Max step up` / `Max step down`; Fly: 26 neighbours. Diagonal moves are rejected when
  either adjacent orthogonal cell is blocked (no corner cutting). `Max search nodes` bounds the work.
- **Smoothing** — string pulling with a 3D voxel line-of-sight walk (Amanatides–Woo) that requires every
  traversed voxel to be navigable, so smoothed paths never cut across pits, through walls or over ledges.
- **Debug** — `Draw voxels` shows navigable (green) / solid (red) voxels, `Draw paths` the last 8 paths.

Unit tests: `AzTestRunner libVoxelNav.Tests.so AzRunUnitTests` (walls, gaps, corner cutting, clearance,
step limits, ceilings, snapping, search limits).
