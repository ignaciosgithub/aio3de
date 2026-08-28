# aio3de — The Complete Manual

A single, detailed reference for every part of this fork of O3DE: installing and
building the engine, using the Editor, every fork-specific feature (ray-traced
shadows, GPU culling, terrain auto-LOD/streaming/occlusion, soft body physics,
the Python AI backbone), scripting and game logic, and a full, end-to-end
walkthrough of the shipped **Arena Shooter example game** — offline play,
neural-network bots with human constraints, server-authoritative internet
multiplayer, match flow, weapons, settings, server browser, team voice chat,
and encrypted anti-tamper networking.

This manual documents **what the fork changes or adds**. For the parts of the
engine the fork leaves as upstream O3DE, use the official O3DE documentation —
see [§1.1 Upstream O3DE documentation](#11-upstream-o3de-documentation).

---

## Table of contents

1. [What this fork is](#1-what-this-fork-is)
2. [Requirements](#2-requirements)
3. [Installing and building the engine](#3-installing-and-building-the-engine)
4. [Creating and configuring a project](#4-creating-and-configuring-a-project)
5. [Editor fundamentals](#5-editor-fundamentals)
6. [Rendering features](#6-rendering-features)
7. [Terrain: auto LOD, streaming, occlusion](#7-terrain-auto-lod-streaming-occlusion)
8. [Physics](#8-physics)
9. [Soft body physics](#9-soft-body-physics)
10. [Particles (OpenParticleSystem)](#10-particles-openparticlesystem)
11. [Input and device detection](#11-input-and-device-detection)
12. [Animation](#12-animation)
13. [Scripting and game logic](#13-scripting-and-game-logic)
14. [AI: the AIBackbone gem](#14-ai-the-aibackbone-gem)
15. [The example game: Arena Shooter](#15-the-example-game-arena-shooter)
16. [Multiplayer networking](#16-multiplayer-networking)
17. [Secure networking (anti-tamper)](#17-secure-networking-anti-tamper)
18. [Match flow, weapons and audio](#18-match-flow-weapons-and-audio)
19. [Settings, server browser and voice chat](#19-settings-server-browser-and-voice-chat)
20. [Troubleshooting](#20-troubleshooting)
21. [Reference tables](#21-reference-tables)
22. [Document index](#22-document-index)

---

## 1. What this fork is

`aio3de` is a fork of the Open 3D Engine (O3DE) focused on making the engine
practical and demonstrable out of the box. On top of upstream O3DE it adds:

| Area | Additions |
| --- | --- |
| Rendering | Hardware-agnostic ray-traced shadows (portable BVH + compute traversal, live-toggleable), GPU-driven frustum culling with indirect draws, SIMD batched CPU sphere culling, PSO pipeline-library disk caching |
| Terrain | Error-driven automatic terrain LOD bounded by screen-space error, terrain as an automatic software occluder, height-aware streamable level chunks (LevelStreaming gem) |
| Physics | Full XPBD soft body system (SoftBodyPhysics gem): CPU and GPU compute solvers, world/rigid/soft-soft collision, in-Editor component |
| AI | AIBackbone gem: in-Editor AI Model Builder, ML stack installer, dataset recorder, ONNX import, portable weights export; NeuralBots gem: runtime MLP bot agent with human constraints |
| Gameplay | ArenaShooter example game kit (input bindings + Lua gameplay scripts), ArenaShooterNet server-authoritative multiplayer, match flow (warm-up/live/win condition/map vote), multiple weapons with scroll-wheel switching, death animation + spatial audio wiring |
| Player-facing systems | GameSettings gem (persistent FOV/sensitivity, runtime key rebinding, in-game settings menu), ServerBrowser gem (master-server discovery + in-game list), VoiceChat gem (team voice with mic capture, mu-law compression, UDP relay) |
| Server operations | ServerAdmin gem: rcon-style remote administration with HMAC challenge-response auth, plus SSH/tunnel guidance |
| Networking | Setup and tooling for the engine's DTLS transport security: RSA-authenticated handshake, AES-GCM encrypted/authenticated packets, replay protection, cert pinning |
| Tooling & onboarding | `o3de doctor/status/resolve` CLI, auto-bootstrapping Python venv, Tkinter pre-build GUI hub, cross-platform quick start, feature test/benchmark harness |
| Parallelism | `AZ::ParallelFor` TaskGraph primitive used across hot paths |
| Client hardening | Server-pushed console commands restricted on clients (`cl_serverConsoleCommandPolicy`), validated master-server and voice-relay input |

Everything is delivered as standard O3DE gems and engine code — nothing needs
external services.

### 1.1 Upstream O3DE documentation

The fork changes a small slice of a very large engine. Anything not covered
here behaves exactly like upstream O3DE 2.7, and the official documentation is
the authoritative reference for it:

| Topic (unchanged by this fork) | Upstream reference |
| --- | --- |
| Editor UI, viewport, manipulators, prefabs | <https://docs.o3de.org/docs/user-guide/editor/> |
| Asset pipeline, Asset Processor, scene settings (FBX/glTF import) | <https://docs.o3de.org/docs/user-guide/assets/> |
| Atom renderer: materials, shaders, lighting, post effects, render pipelines | <https://docs.o3de.org/docs/atom-guide/> |
| Components reference (every built-in component) | <https://docs.o3de.org/docs/user-guide/components/reference/> |
| Script Canvas and Lua scripting APIs | <https://docs.o3de.org/docs/user-guide/scripting/> |
| EMotionFX animation editor and anim graphs | <https://docs.o3de.org/docs/user-guide/visualization/animation/> |
| PhysX rigid bodies, colliders, character controllers | <https://docs.o3de.org/docs/user-guide/interactivity/physics/> |
| Multiplayer gem concepts (net components, autonomous entities, RPCs) | <https://docs.o3de.org/docs/user-guide/networking/> |
| Packaging, project export, platform deployment | <https://docs.o3de.org/docs/user-guide/packaging/> |
| C++ API reference | <https://docs.o3de.org/docs/api/> |
| Gem reference (all shipped gems) | <https://docs.o3de.org/docs/user-guide/gems/reference/> |

There is no official single-file O3DE PDF; the docs site is versioned HTML.
To get an offline/PDF copy, open the section you need and use the browser's
**Print → Save as PDF** (each docs page prints cleanly), or clone
<https://github.com/o3de/o3de.org> and build the site locally. A PDF of *this*
manual can be produced from the Markdown with
`pandoc docs/aio3de/MANUAL.md -o aio3de_manual.pdf`.

When upstream docs and this manual disagree about a fork feature, this manual
wins; for everything else, upstream wins.

---

## 2. Requirements

### Windows

- Windows 10/11, 64-bit.
- **Visual Studio 2022 or 2026** with the "Desktop development with C++"
  workload (MSVC, Windows SDK, CMake support). VS 2022 uses the v143 toolset;
  VS 2026 uses v145 and its `Visual Studio 18 2026` CMake generator needs
  CMake >= 4.1 (CMake preset: `windows-vs2026`).
- **CMake 3.22+** (bundled with VS or standalone).
- **Git** (with LFS if you clone large asset repos).
- **RAM**: 16 GB minimum; **strongly recommended**: a page file of
  16–32 GB (see [§20.1](#201-msvc-out-of-heap-c1060)). Compiling the Editor is
  memory-hungry.
- **Disk**: ~100 GB free for engine + build + 3rd-party packages.
- GPU with DirectX 12 or Vulkan support.

### Linux

- Ubuntu 22.04/24.04 (other distros work with equivalent packages).
- Clang or GCC toolchain, CMake, Ninja.
- Vulkan-capable GPU + drivers for running; building headless is fine.
- See `docs/aio3de/BUILDING_LINUX.md` for the exact package list and the
  tinyusdz/FetchContent notes specific to this fork.

---

## 3. Installing and building the engine

### 3.1 Clone

```bat
git clone https://github.com/ignaciosgithub/aio3de.git
cd aio3de
```

### 3.2 Register the engine

```bat
scripts\o3de.bat register --this-engine        :: Windows
scripts/o3de.sh register --this-engine         # Linux
```

The first `o3de` invocation **auto-bootstraps the Python virtual environment**
(a fork feature) — no manual `get_python` step. If the bundled Python crashes
on launch, the CLI detects it and points you at the missing VC++
redistributable.

Optional health check (fork feature):

```bat
scripts\o3de.bat doctor     :: verify toolchain, packages, registration
scripts\o3de.bat status     :: show engine/project registration state
scripts\o3de.bat resolve    :: attempt automatic fixes
```

### 3.3 Configure (CMake)

Windows:

```bat
cmake -B build\windows -S . -G "Visual Studio 17 2022"
```

Linux:

```bash
cmake -B build/linux -S . -G Ninja -DCMAKE_BUILD_TYPE=profile
```

CMake downloads the required 3rd-party packages automatically on first
configure. **Re-run the configure whenever you enable/disable a gem** — the
gem list is baked into the generated build. Because CMake caches your options,
the re-run is just:

```bat
cmake build\windows
```

### 3.4 Build

```bat
cmake --build build\windows --target Editor --config profile -- /m
```

On memory-constrained machines use serial compilation (see
[§20.1](#201-msvc-out-of-heap-c1060)):

```bat
cmake --build build\windows --target Editor --config profile -- /m:1 /p:CL_MPCount=1
```

Other useful targets: `AssetProcessor`, `<Project>.GameLauncher`,
`<Project>.ServerLauncher`, `o3de` (Project Manager GUI).

A successful build ends with `Build succeeded` and
`Editor.vcxproj -> ...\bin\profile\Editor.exe`.

### 3.5 First launch

1. Start `build\windows\bin\profile\Editor.exe` (it launches the Asset
   Processor automatically).
2. **Let the Asset Processor finish** before judging anything — on a first
   launch or after a big rebuild it processes thousands of assets and shader
   variants; the Editor can look frozen for several minutes. Watch the AP tray
   icon's job count.
3. Open or create a level; press **Ctrl+G** to enter game mode, **Esc** to
   leave.

---

## 4. Creating and configuring a project

### 4.1 Create

GUI: run `build\windows\bin\profile\o3de.exe` (Project Manager) → *New
project*. This fork also ships a Tkinter **pre-build onboarding hub**
(preflight checks + project creation) for machines where the Qt Project
Manager isn't built yet.

CLI:

```bat
scripts\o3de.bat create-project -pp C:\path\to\MyProject
```

### 4.2 Enable gems

Every feature in this manual lives in a gem. Two ways to enable one:

CLI (from the engine root):

```bat
scripts\o3de.bat enable-gem -gn <GemName> -pp <project path>
```

GUI: Project Manager → project card → **⋮ → Configure Gems** → toggle → save.
The onboarding hub has the same thing in its **Gems** tab (pick the project,
toggle gems), and on the CLI `scripts/o3de.sh hub gems -pp <project>
[--enable NAME] [--disable NAME]` lists and flips them in one command.

**Rules that trip everyone up:**

- *Asset-only gems* (e.g. `ArenaShooter`) need no rebuild — restart the
  Editor/Asset Processor and the assets appear.
- *Code gems* (e.g. `NeuralBots`, `ArenaShooterNet`, `OpenParticleSystem`,
  `SoftBodyPhysics`, `LevelStreaming`) require: **enable → re-run CMake
  configure → rebuild the Editor → relaunch**. Their components do **not**
  appear in *Add Component* until the rebuilt Editor is running.
- You can verify what's enabled by opening `<project>/project.json` and
  reading the `gem_names` array.

### 4.3 Gems used in this manual

| Gem | Type | Provides |
| --- | --- | --- |
| `ArenaShooter` | assets | example game scripts + input bindings |
| `NeuralBots` | code | neural-net Bot Agent component |
| `ArenaShooterNet` | code | server-authoritative multiplayer components |
| `AIBackbone` | code+tools | AI Model Builder, training, dataset recorder |
| `SoftBodyPhysics` | code | Soft Body component (XPBD) |
| `LevelStreaming` | code | streamable chunk grid component |
| `OpenParticleSystem` | code | Particle component + Particle Editor |
| `Multiplayer` | code | netcode foundation (dependency of ArenaShooterNet) |
| `GameSettings` | code | persistent settings + Remappable Input component |
| `ServerBrowser` | code | master-server announce/browse + join |
| `VoiceChat` | code | team voice capture, relay and playback |
| `ServerAdmin` | code | rcon-style remote server administration |
| `Microphone` | code | microphone capture (dependency of VoiceChat) |
| `MiniAudio` | code | sound playback, 3D/positional audio |
| `PhysX5` | code | rigid bodies, character controller, scene queries |
| `StartingPointInput` | code | Input component / input event buses |
| `EMotionFX` | code | Actor + Anim Graph animation |
| `DebugDraw` | code | on-screen debug text/shapes (used by the HUD) |
| `WhiteBox` | code | in-Editor blockout modeling |
| `CSharpScripting` | code | Unity-style C# scripting on .NET 8 (CoreCLR) |
| `FluidDynamics` | code | particle fluids (PBF) + analytic wind |
| `Replay` | code | Quake-demo-style record/playback of tracked entities |
| `VideoTexture` | code | video files as textures (in-game screens/TVs) |
| `LLMAssist` | tools | in-Editor AI assistant + Gem Manager panel |

---

## 5. Editor fundamentals

- **Entity Outliner** (left): the level's entity hierarchy. Right-click →
  *Create entity*. Drag entities to parent them.
- **Entity Inspector** (right): components of the selected entity. **Add
  Component** button → searchable list grouped by category.
- **Asset Browser**: every processed asset, organized by gem/project folder.
  Assets from a gem appear under `Gems/<GemName>/Assets/...`.
- **Viewport**: WASD + right-mouse-drag to fly. `1/2/3` translate/rotate/scale
  gizmos.
- **Prefabs**: select entities → right-click → *Create Prefab*. Prefabs are
  the unit of reuse and of network spawning.
- **Game mode**: **Ctrl+G** starts simulation in-Editor, **Esc** exits.
- **Eject / possess (F10)**: while in game mode, press **F10** to eject from
  the game camera — the game keeps running while the editor camera flies free
  and the editor UI (Outliner, Inspector, Console) becomes usable again.
  Press **F10** again to possess the active game camera. Note: while ejected,
  the game still receives keyboard/gamepad input.
- **Console** (bottom): shows errors and accepts cvar input (e.g. type
  `r_rayTracedShadows true`).
- **White Box modeling**: add a **White Box** component to an entity to
  push/pull faces, extrude and split edges directly in the viewport — ideal
  for blocking out level geometry. For organic/character modeling use Blender
  and import FBX/glTF; the Asset Processor ingests them automatically.

The step-by-step "empty editor → playable level" walkthrough (terrain,
lighting, physics props, player camera) is in
`docs/aio3de/GETTING_STARTED_TUTORIAL.md`.

---

## 6. Rendering features

### 6.1 Hardware-agnostic ray-traced shadows

Unlike vendor RT APIs, this implementation runs on **any GPU with compute
shaders**: a portable BVH is built on the CPU (asynchronously, on background
jobs) and traversed in a compute pass, producing hard ray-traced shadows
composited over the main pipeline.

Runtime control (Editor console or `.cfg` files):

| CVar | Type | Meaning |
| --- | --- | --- |
| `r_rayTracedShadows` | bool | master toggle (live-toggleable) |
| `r_rayTracedShadowsRebuild` | bool | force a BVH rebuild now |
| `r_rayTracedShadowsAutoRebuild` | bool | rebuild automatically when meshes move/change |
| `r_rayTracedShadowsAutoRebuildPollFrames` | uint | frames between change-detection polls |
| `r_rayTracedShadowsPrewarm` | bool | build the BVH at level load (no first-toggle hitch) |
| `r_rayTracedShadowsBias` | float | ray origin offset to fight self-shadow acne |
| `r_rayTracedShadowsFactor` | float | shadow darkness factor |
| `r_rayTracedShadowsMaxDistance` | float | max shadow ray distance |
| `r_rayTracedShadowsMaxTriangles` | uint | BVH triangle budget |

Design notes:

- The BVH build runs **asynchronously** on background jobs; toggling shadows or
  moving meshes never hitches the frame.
- BVH buffers are bound directly to the pass SRG (read-only buffers have no
  attachment id) — relevant if you write your own consumer pass.
- Details: `docs/aio3de/RAY_TRACING.md`.

### 6.2 GPU-driven frustum culling

A compute pass classifies instance bounds against the frustum and compacts
survivors into `DrawIndexedIndirect` argument buffers consumed by a dedicated
draw pass — the CPU never touches per-instance visibility. See
`docs/aio3de/GPU_CULLING.md` for the pass wiring and how to feed it.

### 6.3 CPU batched sphere culling

`AZ::FrustumClassifySpheres` classifies spheres against a frustum in
SoA + SIMD batches, integrated (gated) into `RPI::Culling`:

| CVar | Meaning |
| --- | --- |
| `r_useBatchedSphereCulling` | enable the SoA/SIMD batched path |
| `r_batchedSphereCullingMinEntries` | minimum batch size before it kicks in |
| `r_useEntryWorkListsForCulling` | work-list based job splitting |
| `r_numEntriesPerCullingJob` / `r_numNodesPerCullingJob` | job granularity |

### 6.4 PSO precaching

The RHI pipeline-library disk cache is **on by default** in this fork:
compiled pipeline states persist across runs, eliminating most first-frame
shader-compile hitches. A console command saves the cache on demand, and an
auto-save interval writes it periodically.

### 6.5 CPU parallelism

`AZ::ParallelFor` (in `AzCore/Task/Algorithms.h`) is a TaskGraph-based
parallel-for used across engine hot paths (culling, soft body sweeps, BVH
builds). Use it for your own data-parallel loops:

```cpp
#include <AzCore/Task/Algorithms.h>
AZ::ParallelFor(0, count, [&](size_t i) { /* work on element i */ });
```

### 6.6 Camera render-to-texture (security cameras, mirrors, portals)

Any camera can render its view onto a texture that other materials sample —
in the Editor *and* in game:

1. Create a render target texture: add a file `MyScreen.attimage` to your
   project's assets:

   ```json
   {
       "Type": "JsonSerialization",
       "Version": 1,
       "ClassName": "AttachmentImageAsset",
       "ClassData": {
           "m_imageDescriptor": {
               "BindFlags": [ "ShaderRead", "ShaderWrite", "Color" ],
               "Size": { "Width": 512, "Height": 512 },
               "Format": 19 // R8G8B8A8_UNORM
           },
           "Name": "$MyScreen",
           "IsUniqueName": true
       }
   }
   ```

2. On the camera entity's **Camera** component, set **Target texture** to
   `MyScreen.attimage`. The camera now renders into the texture every frame
   (this works at runtime in the GameLauncher too — the pipeline is created
   when the camera entity activates).
3. On the "screen" mesh, create a material and set its **Base Color > Texture**
   (or Emissive texture) to the same `MyScreen.attimage`.

**Update rate**: the **Update rate** setting on the Camera component limits how
often a render-to-texture (or picture-in-picture) camera renders. `0` renders
every frame; `30` suits minimaps; values below 1 are allowed (`0.5` = one
render every 2 seconds, e.g. a slow security monitor). The main view always
renders at full rate.

**Picture-in-picture (minimaps, rear-view mirrors)**: enable
**Picture-in-picture** on any Camera component to draw its view as an overlay
rectangle on top of the main view — no `.attimage` or material setup needed.
Configure:

- **Overlay position / size** — normalized screen coordinates (`0,0` =
  top-left, `1,1` = bottom-right).
- **Resolution width / height** — the resolution the camera renders at,
  independent of the overlay's on-screen size. Lower is faster.
- **Update rate** — see above; `30` or less is typical for minimaps.

Picture-in-picture works in the Editor viewport and in game. Several PiP
cameras can be active at once, each with its own resolution and update rate.

### 6.7 Video textures (VideoTexture gem)

The **VideoTexture** gem plays video files onto the same render target
textures, so any mesh can be a video screen. Enable the `VideoTexture` gem,
then:

1. Convert your video to MPEG-1 (the built-in, dependency-free codec):

   ```bash
   ffmpeg -i input.mp4 -c:v mpeg1video -q:v 5 -an output.mpg
   ```

2. Drop `output.mpg` in your project assets (Asset Processor packages it).
3. Use the gem's ready-made render target `VideoTextureTarget.attimage`
   (512x512), or create your own `.attimage` sized to the video (see above).
   Note: `.attimage` files are empty GPU render targets described in JSON —
   they are not images and cannot be made from a png.
4. Add a **Video Texture** component to an entity; set **Video** to the `.mpg`
   and **Target texture** to the `.attimage`; assign the `.attimage` to the
   screen mesh's material.
5. Playback options: **Play on start**, **Loop**, **Playback speed** — or drive
   it from Lua/Script Canvas via `VideoTextureRequestBus`
   (`Play`, `Pause`, `Stop`, `SetLooping`, `SetPlaybackSpeed`).

---

## 7. Terrain: auto LOD, streaming, occlusion

### 7.1 Terrain auto LOD (Nanite-style error-driven)

Each terrain sector selects its LOD from the **actual geometric error** it
would introduce, projected to screen space — not from raw distance. You set a
tolerance in *pixels*; the mesh manager picks, per sector, the coarsest LOD
whose projected error stays below it. Silhouettes stay crisp near the camera
while flat, distant terrain drops to very coarse meshes.

Settings (on the Terrain World Renderer component):

| Setting | Default | Meaning |
| --- | --- | --- |
| Auto LOD | on | error-driven selection (off = legacy distance-based) |
| Auto LOD error (pixels) | 1.0 | max tolerated screen-space error; raise for performance, lower for quality |

Debug cvars: `r_debugTerrainLodLevels` (color-code sectors by LOD),
`r_debugTerrainAabbs` (draw sector bounds).

### 7.2 Level streaming (LevelStreaming gem)

Add a **Level Streaming** component to a level entity to stream world content
in/out around the camera in a chunk grid. The differentiator: **chunk bounds
grow vertically to the tallest object inside the chunk**, so a tall tower
keeps streaming from much further away than a crate — distance is measured to
the chunk's true bounds, not to a flat cell.

| Setting | Default | Meaning |
| --- | --- | --- |
| Chunk size | 64 m | horizontal (XY) size of a streamable chunk |
| Stream distance | 256 m | camera-to-chunk-bounds distance under which the chunk streams in |
| Hysteresis | 0.1 | extra fraction beyond stream distance before streaming out (kills boundary flicker) |
| Rebuild interval | 0.5 s | how often chunk membership is rebuilt (picks up moved/spawned objects) |

### 7.3 Terrain occlusion

Terrain is fed into the software occlusion buffer as an **automatic,
conservative occluder**: hills and ridges cull the entities behind them with
no manual occluder authoring. It is conservative — it never over-culls; worst
case it merely fails to cull. Works together with (and independently of) the
GPU culling path.

---

## 8. Physics

The engine uses **PhysX 5** (`PhysX5` gem). The pieces you'll use constantly:

- **Static geometry**: *PhysX Static Rigid Body* + *PhysX Collider* (or a
  Terrain physics collider). Objects with colliders but no dynamic rigid body
  are immovable and collide with everything dynamic.
- **Dynamic props**: *PhysX Dynamic Rigid Body* + *PhysX Collider* (set mass,
  restitution, friction on the collider's physics material).
- **Characters**: *PhysX Character Controller* (+ *Character Gameplay* for
  gravity/jumping). All player and bot movement in the example game goes
  through the character controller — never write transforms directly on
  physics-driven entities.
- **Triggers**: any collider with *Trigger* checked; script against
  trigger-enter/exit notifications.
- **Scene queries**: raycasts/sweeps from Lua, Script Canvas, or C++ — used by
  the example game for hitscan shots and bot line-of-sight.

---

## 9. Soft body physics

The `SoftBodyPhysics` gem adds a **Soft Body** component that simulates any
mesh as an XPBD (extended position-based dynamics) particle system: edges
become distance constraints, an optional pressure constraint preserves volume,
and the render mesh deforms with the simulation. Fully set up in-Editor, no
code needed.

### 9.1 Setup

1. Enable `SoftBodyPhysics`, reconfigure, rebuild.
2. Entity with a **Mesh** component → **Add Component → Soft Body**.
3. Press Ctrl+G — the mesh drops, jiggles, and collides per your settings.

### 9.2 Settings reference

| Setting | Default | Meaning |
| --- | --- | --- |
| Mass per vertex | 0.1 kg | mass of each simulated particle |
| Compliance | 0.001 | edge softness; 0 = rigid edges, larger = stretchier |
| Pressure | 1.0 | volume preservation; 0 = off, 1 = keep rest volume, >1 inflates |
| Damping | 0.5 | per-second velocity damping [0..1] |
| Substeps | 4 | simulation substeps per frame |
| Iterations | 4 | constraint iterations per substep |
| Gravity scale | 1.0 | multiplier on world gravity |
| Solver mode | CPU | `Cpu` (all features) or `Gpu` compute shader (Vulkan/DX12; Simple collision only) |
| Collision mode | Simple | `Simple` (ground plane), `World` (static level colliders), `WorldAndRigid` (plus two-way dynamic rigid body coupling) |
| Particle radius | 0.02 m | collision thickness per particle in World modes |
| Auto contact thickness | on | grows the thickness from the mesh edge length so contacts cover inter-particle gaps (prevents visible interpenetration) |
| World friction | 0.5 | tangential friction on world contacts |
| Rigid push scale | 1.0 | scale on impulses applied to dynamic rigid bodies |
| Rigid max push velocity | 2.0 m/s | clamp on per-contact velocity change imparted to a rigid body (prevents explosions at play start) |
| Soft-soft collision | off | collide with particles of other soft bodies (spatial-hash contacts) |
| Soft-soft friction | 0.5 | friction for soft-soft contacts |
| Ground collision / height / friction | on / 0 / 0.5 | the Simple-mode ground plane |
| Pin highest vertices | off | pin topmost vertices (hanging cloth/bodies) |
| Pin tolerance | 0.01 m | how close to the top a vertex must be to pin |

### 9.3 How the collision modes work

- **Simple** — particles collide with a horizontal plane. Cheapest; the only
  mode the GPU solver supports.
- **World** — particles sphere-sweep against the level's static PhysX
  colliders with MTD depenetration (rigid geometry cannot pass through the
  soft body). Collider-AABB broadphase culling keeps the sweep count low.
- **WorldAndRigid** — adds *two-way* coupling with dynamic rigid bodies: the
  soft body is pushed by them and pushes back (impulse clamped by
  *Rigid max push velocity*).

### 9.4 CPU vs GPU solver

The GPU solver runs the XPBD constraint solve in a compute shader — use it for
high-resolution meshes where the CPU solve dominates the frame. Trade-off: GPU
mode currently supports Simple collision only. The CPU solver parallelizes
across cores via `AZ::ParallelFor` and supports every feature. The mode is a
per-component dropdown, so you can A/B them live.

### 9.5 Fluid dynamics (FluidDynamics gem)

The **FluidDynamics** gem simulates particle fluids (position-based fluids)
on the CPU. Enable the `FluidDynamics` gem, then add a **Fluid Volume**
component to an entity:

- **Preset**: *Water*, *Honey* (viscous), or *Custom* (hand-tune rest
  density, viscosity, particle spacing, substeps/iterations, damping).
- **Spawn half extents**: the box (local space) filled with particles at
  activation.
- **Container enabled**: on = particles stay inside the container box
  (with adjustable wall restitution); off = the fluid spills freely under
  gravity.
- **Affected by wind**: couple the fluid to any **Wind** component in the
  level (analytic wind field with drag).
- Solvers parallelize across cores (`Parallel` toggle) and particles render
  through the built-in visualization.

---

## 10. Particles (OpenParticleSystem)

The engine ships the **OpenParticleSystem** gem (from o3de-extras): a full
CPU-simulated particle system with an in-Editor authoring tool.

1. Enable `OpenParticleSystem` for your project, reconfigure, rebuild, relaunch.
2. Let the Asset Processor finish (the gem has shaders/materials/samples to
   compile).
3. Entity → **Add Component → Particle System → Particle**; assign a sample
   `.particle` asset (search `firework` in the picker; samples live in
   `Gems/OpenParticleSystem/Assets/Particles/`).
4. Author your own: **Tools → (Preview) Particle Editor** — emitters with
   spawn-rate/burst modules, shapes, forces, vortex, noise, color/size over
   life, collision, events; sprite, ribbon-trail and mesh renderers.

If the Particle component is missing from *Add Component*, the gem is not in
your Editor build — see [§20.4](#204-a-gems-component-doesnt-appear-in-add-component).

---

## 11. Input and device detection

Input flows through the `StartingPointInput` gem:

1. An **`.inputbindings` asset** maps physical inputs (keys, mouse axes,
   gamepad sticks/buttons) to named **input events** (e.g. `MoveForward`).
   Multiple devices can map to the *same* event simultaneously — that's how
   the example game supports keyboard/mouse and gamepad at once with zero
   toggles.
2. An **Input component** on an entity activates a bindings asset.
3. Scripts/components listen for the named events
   (`InputEventNotificationBus`), receiving pressed/held/released with an
   analog value.

The ArenaShooter kit's `arenashooter.inputbindings` defines:

| Event | Keyboard/Mouse | Gamepad |
| --- | --- | --- |
| `MoveForward` | W/S | left stick Y |
| `MoveRight` | A/D | left stick X |
| `LookX` / `LookY` | mouse delta | right stick |
| `Shoot` | left mouse button | right trigger |
| `Jump` | Space | A button |
| `KMActivity` / `PadActivity` | any KB/M input | any pad input |

**Device detection**: `DeviceDetector.lua` listens to the two activity events
and broadcasts a gameplay event `ActiveInputDevice` (1.0 = keyboard/mouse,
2.0 = gamepad) whenever the player switches device — hook it to swap UI button
prompts or aim sensitivity.

---

## 12. Animation

Character animation uses **EMotionFX**:

1. Import a rigged character (FBX) — the Asset Processor produces an **Actor**.
2. Add **Actor** + **Anim Graph** components to the character entity.
3. Author states/blends in the **Animation Editor**; expose **float
   parameters** used as transition conditions.

The example game's `AnimationDriver.lua` drives two parameters every frame:

- `MoveSpeed` — planar speed in m/s → drive idle/walk/run blend trees.
- `Shooting` — 1 while firing → drive a shoot/recoil state.

Create parameters with those names in your anim graph, wire your transitions
to them, and any rigged character animates correctly with zero extra code. If
the Actor is a child of the moving entity, point the script's *SourceEntity*
at the moving root.

---

## 13. Scripting and game logic

Three first-class scripting options, usable together:

- **Lua** (Lua Script component) — full API surface, best for gameplay systems.
- **Script Canvas** — node-based visual scripting, same underlying buses.
- **C#** (C# Script component, `CSharpScripting` gem) — Unity-style scripts on
  .NET 8; see [§13.2](#132-c-scripting-csharpscripting-gem).

The core mental model is the **EBus**: components and scripts communicate by
sending/handling events on addressable buses (per-entity or broadcast). The
example game standardizes on **gameplay notification events** with a float
payload — any Lua script, Script Canvas graph, or C++ component can raise or
handle them:

```lua
-- send 25 damage to entity `target`
GameplayNotificationBus.Event.OnEventBegin(
    GameplayNotificationId(target, "Damage", typeid(0.0)), 25.0)
```

| Event | Channel entity | Meaning |
| --- | --- | --- |
| `Damage` | damaged entity | apply damage (value = amount) |
| `Killed` | dying entity | entity just died |
| `Respawned` | respawned entity | entity came back |
| `MoveSpeed` | player entity | planar speed every tick |
| `ActiveInputDevice` | detector entity | 1 = KB/M, 2 = gamepad |

Copy-paste recipes (WASD movement, prefab spawning, triggers, timers, Script
Events, camera control, debugging) and the anatomy of a Lua component are in
`docs/aio3de/SCRIPTING_AND_GAME_LOGIC.md`.

### 13.1 Replay / demo recording (Replay gem)

The **Replay** gem records tracked entities into demo files (Quake demo
style) and plays them back — in the Editor's game mode or in the launcher.
Enable the `Replay` gem, then:

1. Add a **Replay Tracker** component to every entity you want recorded.
   Settings: **Track name** (empty = entity name; playback matches entities
   by this name, keep it unique) and **Sample rate** (samples per second,
   0 = every frame; 10–30 fps is plenty for most objects).
2. While the game is running, record and play back with console commands:

   | Command | Effect |
   | --- | --- |
   | `replay_record <name>` | start recording all tracked entities |
   | `replay_stop` | stop and save `<project>/user/Replays/<name>.replay` |
   | `replay_play <name>` | play a demo back, driving the tracked entities |
   | `replay_stop_playback` | stop playback where it is |
   | `replay_pause` / `replay_pause 0` | pause / resume playback |
   | `replay_seek <seconds>` | scrub to a time |
   | `replay_speed <multiplier>` | slow-mo / fast-forward playback |

3. The same controls are scriptable from Lua/Script Canvas and the Editor
   Python console via `ReplayRequestBus` (`StartRecording`, `StopRecording`,
   `StartPlayback`, `SeekPlayback`, `SetPlaybackSpeed`,
   `GetPlaybackTime`, `GetPlaybackDuration`, ...).

Demos store world-transform keyframes (position, rotation, uniform scale)
per track and interpolate between them on playback, so files stay small.
During playback the recorded entities are driven kinematically; entities
that can't be matched by name are skipped with a warning.

### 13.2 C# scripting (CSharpScripting gem)

The **CSharpScripting** gem hosts .NET 8 (CoreCLR) in-process, on Linux and
Windows. Requirements: the .NET 8 SDK (`sudo apt install dotnet-sdk-8.0` on
Ubuntu, or the installer from dotnet.microsoft.com).

Setup:

1. Enable the `CSharpScripting` gem (code gem → reconfigure + rebuild).
2. Put `.cs` files in `<project>/Scripts`.
3. Add a **C# Script** component to an entity and set **Class name**.

Scripts derive from `AIO3DE.ScriptComponent` (Unity `MonoBehaviour`-style):

```csharp
using AIO3DE;

public class Mover : ScriptComponent
{
    public override void OnActivate() { Debug.Log($"hello from {Entity.Name}"); }
    public override void OnUpdate(float deltaTime) { /* runs every frame */ }
    public override void OnCollisionEnter(Collision c) { Debug.Log($"hit {c.Other.Name}"); }
    public override void OnDeactivate() { }
}
```

API surface (`AIO3DE.Core`): transforms (world/local position, quaternion
rotation, basis vectors, parenting), entity lifecycle (`Entity.Find`,
`Entity.Create`, `Destroy`, `SetActive`), tags (`HasTag`/`AddTag`/`RemoveTag`,
`Entity.FindByTag`/`FindAllByTag`), input (keys, mouse, any O3DE input channel
including gamepads), physics (`Physics.Raycast`, rigid-body velocity /
impulses / mass / gravity / kinematic), collision callbacks
(`OnCollisionEnter`/`OnCollisionExit`), prefab instantiation
(`Prefab.Spawn("prefabs/enemy.spawnable", position)` → async `PrefabInstance`
with `RootEntity` and `Despawn()`), `Time`, `Debug` logging, and full
`Vector3`/`Quaternion` math.

Scripts compile automatically with `dotnet build`; recompile with the
component's **Rebuild scripts** button or the `csharp_rebuild` console
command. Hot reload is safe: scripts live in a collectible
`AssemblyLoadContext`, so a rebuild deactivates old instances and unloads the
old assembly. Full reference and samples (`Mover.cs`, `FpsController.cs`,
`Spawner.cs`): `Gems/CSharpScripting/README.md`.

### 13.3 In-Editor AI assistant (LLMAssist gem)

The **LLMAssist** gem (script-only, no rebuild) adds two Editor panes under
**Tools**: **AI Assistant** — chat with OpenAI/Anthropic/Kimi models, with an
optional docs-aware mode that feeds this fork's documentation into the
conversation and an *Apply file edits* button for `FILE:` blocks in replies
(with backups) — and **Gem Manager** for per-project gem toggling. API keys go
in the Settings tab (stored in `~/.o3de/llmassist_keys.json`, never
committed); environment variables like `OPENAI_API_KEY` also work. Details:
`Gems/LLMAssist/README.md`.

---

## 14. AI: the AIBackbone gem

`AIBackbone` puts a machine-learning workflow inside the Editor:

- **AI Model Builder** (Editor tool): define an MLP — input/output widths,
  hidden layers, activations — as a model spec.
- **ML stack installer**: installs the required Python packages (PyTorch etc.)
  into the engine's Python environment on demand.
- **Dataset recorder**: record observation/action pairs from gameplay for
  supervised/imitation training.
- **Training**: trains with PyTorch; saves `.pt`, attempts `.onnx` export, and
  **exports `<model>.weights.json>`** — a portable, dependency-free weights
  format consumed at runtime by the NeuralBots gem.
- **ONNX import** with automatic I/O shape detection.

### The `.weights.json` format (`mlp-1`)

```json
{
  "format": "mlp-1",
  "layers": [
    { "weights": [[...],[...]], "biases": [...], "activation": "relu" }
  ]
}
```

- `weights` is row-major `[out_units][in_units]`; `biases` has `out_units`
  entries; layer widths must chain.
- Supported activations: `relu`, `tanh`, `sigmoid`, `leaky_relu`, `none`.
- The runtime loader (`NeuralBots`) validates format, widths, raggedness and
  bias counts, and refuses (with a warning) rather than misbehave.

---

## 15. The example game: Arena Shooter

The fork ships a complete example game in three gems, built in layers so each
is useful alone. This section is the full walkthrough.

### 15.1 Overview and architecture

| Layer | Gem | What it adds |
| --- | --- | --- |
| Offline game | `ArenaShooter` (assets) | bindings, movement, shooting, health, HUD, device detection, animation driver |
| AI opponents | `NeuralBots` (code) | neural-net Bot Agent with human constraints |
| Multiplayer | `ArenaShooterNet` (code) | server-authoritative networked player + health |
| Anti-tamper | engine + docs | DTLS encryption with RSA-authenticated handshake ([§17](#17-secure-networking-anti-tamper)) |

Everything communicates through the shared gameplay events of §13, so humans,
bots, offline scripts and networked components all damage and kill each other
symmetrically.

### 15.2 Phase 1 — the offline game (ArenaShooter kit)

**Enable** (asset-only, no rebuild):

```bat
scripts\o3de.bat enable-gem -gn ArenaShooter -pp <your project path>
```

Restart the Editor; the files appear in the Asset Browser under
`Gems/ArenaShooter/Assets`. Required gems in the project: `PhysX5`,
`StartingPointInput`, `DebugDraw`.

**Shipped example project files:**

| File | Purpose |
| --- | --- |
| `InputBindings/arenashooter.inputbindings` | KB/M + gamepad bindings (both live at once) |
| `Scripts/ArenaShooter/PlayerController.lua` | move/look/jump on the PhysX character controller |
| `Scripts/ArenaShooter/Weapon.lua` | hitscan weapon: raycast from camera, damage + knockback, fire-rate limit |
| `Scripts/ArenaShooter/Health.lua` | health, `Damage` handling, death, timed respawn at a spawn point |
| `Scripts/ArenaShooter/ScoreHud.lua` | score + match timer HUD (DebugDraw), win condition |
| `Scripts/ArenaShooter/DeviceDetector.lua` | broadcasts `ActiveInputDevice` on device switch |
| `Scripts/ArenaShooter/AnimationDriver.lua` | feeds `MoveSpeed`/`Shooting` to an EMotionFX anim graph |

**Build the arena level:**

1. **Arena**: a large box (PhysX Static Rigid Body + Collider) or Terrain for
   the floor; block out walls/ramps/cover with **White Box** components.
2. **Player** entity:
   - PhysX **Character Controller** + **Character Gameplay**
   - **Input** component → `arenashooter.inputbindings`
   - **Lua Script** → `PlayerController.lua` (set *CameraEntity*)
   - **Lua Script** → `Weapon.lua` (same *CameraEntity*)
   - **Lua Script** → `Health.lua`
   - child entity with a **Camera** component at eye height
3. **Targets**: any entity with a PhysX Collider (+ Dynamic Rigid Body if it
   should be knocked around) + `Health.lua`. Set *SpawnPoint* to an empty
   entity to control where it respawns.
4. **Game manager** entity: `ScoreHud.lua` (add every target to *Targets*, set
   *MatchTime*), `DeviceDetector.lua`, and an **Input** component with the same
   bindings (so device-activity events reach the detector).
5. **Ctrl+G** — WASD/left stick to move, mouse/right stick to aim, LMB/right
   trigger to shoot, Space/A to jump. Score and timer render on screen; kill
   all targets or run out the clock.

**Animated characters**: give a rigged character (Actor + Anim Graph)
`AnimationDriver.lua` and create `MoveSpeed`/`Shooting` float parameters in the
anim graph ([§12](#12-animation)).

### 15.3 Phase 2 — neural-net bots (NeuralBots gem)

**Enable** (code gem → reconfigure + rebuild):

```bat
scripts\o3de.bat enable-gem -gn NeuralBots -pp <your project path>
cmake build\windows
cmake --build build\windows --target Editor --config profile
```

**Bot entity setup:**

1. Entity with a PhysX **Character Controller**.
2. **Add Component → AI → Bot Agent**.
3. Set **Target entity** (the player, or another bot for AI-vs-AI matches).
4. Add `Health.lua` so the bot can die and respawn.
5. Leave **Model file** empty → the built-in chase/strafe/shoot heuristic
   drives it; bots fight out of the box (and you can farm training data
   against them).

**Human constraints** (all tunable per bot):

| Constraint | Default | Effect |
| --- | --- | --- |
| Reaction time | 0.20 s | the bot perceives a *delayed* world snapshot — it aims where you were 200 ms ago |
| Aim error | 2.5° | Gaussian noise (Box-Muller) on every shot's yaw/pitch |
| Max turn speed | 360°/s | hard cap on view rotation — no instant 180° flicks |
| Max shots per second | 5 | fire-rate cap |

Additionally the bot **only shoots with real line of sight** (physics
raycast) — no wallhacks — and acts exclusively through player channels:
character-controller movement, capped rotation, and hitscan shots that send
the same `Damage` events as `Weapon.lua`. Bots and humans are mechanically
symmetric.

**Neural policy contract** (fixed):

- **Inputs (8 floats)**: target direction in bot-local space (x, y, z),
  distance/50, line-of-sight (0/1), own speed/10, cos(angle to target),
  time-since-seen/2.
- **Outputs (5 floats)**: strafe (−1..1), forward (−1..1), turn (−1..1 of max
  turn speed), shoot (>0.5 fires), jump (reserved).

**Training workflow** (AIBackbone, §14):

1. AI Model Builder → new model, 8 float inputs, 5 float outputs (e.g. two
   hidden layers of 32, `tanh` output).
2. Record a dataset (e.g. your own play via the recorder) and train.
3. Training exports `<model>.weights.json` next to the `.pt`.
4. Put it in your project; set the Bot Agent's **Model file** to the
   project-relative path (e.g. `AIModels/pvp_bot.weights.json`).
5. Wrong widths or missing file → warning + automatic heuristic fallback.

### 15.4 Phase 3 — multiplayer (ArenaShooterNet gem)

See [§16](#16-multiplayer-networking) for the netcode model, then:

**Enable** (code gem → reconfigure + rebuild). Requires `Multiplayer`,
`PhysX5`, `StartingPointInput` and the `ArenaShooter` gem (for the bindings
asset).

**Networked player prefab** — one entity with, in order:

1. PhysX **Character Controller**
2. **Network Binding** (Multiplayer gem)
3. **Network Transform**
4. **Network Character**
5. **Network Arena Player** (this gem)
6. **Network Arena Health** (this gem)
7. **Input** component → `arenashooter.inputbindings`

Save as a prefab and register it as the spawnable player (e.g. **Simple
Network Player Spawner** component on a level entity).

**Component behavior:**

- **Network Arena Player** — the autonomous client samples the ArenaShooter
  input events (`MoveForward`, `MoveRight`, `LookX`, `LookY`, `Shoot`) into
  network inputs each tick. Movement runs through the *networked* PhysX
  character controller on both the predicting client and the server;
  mispredictions are corrected automatically. **Shots resolve only on the
  server**: the client transmits trigger state; the server enforces the fire
  interval and performs the hitscan raycast itself. Tunables (archetype
  properties): `MoveSpeed`, `EyeHeight`, `FireRange`, `FireDamage`,
  `FireInterval`.
- **Network Arena Health** — health lives on the server and replicates to all
  clients (clients cannot write it). Damage comes only from server-side shot
  resolution. Death stashes the body and respawns at the spawn point after
  `RespawnDelay`. Tunables: `MaxHealth`, `RespawnDelay`.

**Running server + client:**

```bat
:: machine A (server)
<Project>.ServerLauncher.exe --console-command-file=server.cfg
::   server.cfg:  host          (add "sv_port 33450" to pick a port)

:: machine B (client)
<Project>.GameLauncher.exe --console-command-file=client.cfg
::   client.cfg:  connect <server ip>:33450
```

Test locally first (both on one machine, `connect 127.0.0.1`). For internet
play: forward the UDP port on the server's router and verify it with an
online port checker *before* trying remote clients.

**Bots online**: spawn bot entities server-side; they act through the same
validated gameplay channels, so they need no special netcode.

### 15.5 Phase 4 — anti-tamper

Covered in full in [§17](#17-secure-networking-anti-tamper). Summary of the
two-layer model:

1. **Server authority** (phase 3): the server owns position, health, damage
   and fire rate; clients send only inputs. A tampered client can at most send
   *legal* inputs.
2. **Transport hardening** (phase 4): RSA-authenticated DTLS handshake +
   AES-256-GCM encrypted and authenticated packets + replay windows + cert
   pinning. Nobody between the client and server can read, alter, or replay
   traffic.

Both layers are required: encryption cannot stop a malicious client from
sending valid-but-dishonest data — that is what server authority is for.

---

## 16. Multiplayer networking

The `Multiplayer` gem (on `AzNetworking`) provides:

- **Roles**: an entity is *authority* (server), *autonomous* (the owning
  client), or *client* (everyone else's replica).
- **Network inputs**: the autonomous client creates inputs
  (`CreateInput`), sends them to the server, and both run `ProcessInput` —
  the client predictively, the server authoritatively. Server results correct
  client mispredictions automatically.
- **Network properties**: state replicated authority → clients (e.g.
  `Health`).
- **Multiplayer auto-components**: components declared in XML
  (`*.AutoComponent.xml`); codegen produces the networking plumbing. This is
  how `ArenaShooterNet` defines its two components — copy that gem as a
  template for your own networked gameplay.

Key rules baked into the example (follow them in your own games):

- Clients **never** send positions, hit results, or damage — only inputs.
- Anything gameplay-critical (hit detection, cooldowns, health) executes on
  the server, using server-side scene queries.
- Movement of networked, physics-driven entities goes through the networked
  character controller (`TryMoveWithVelocity`), never direct transform writes.

Useful console commands/cvars: `host`, `connect <ip>:<port>`, `disconnect`,
`sv_port` (server), plus the security cvars of §17.

### Remote server administration (rcon)

The **ServerAdmin** gem gives dedicated servers an rcon-style remote
console: set `admin_password <secret>` and `admin_enable true` in the
server cfg (default TCP port `admin_port 33470`), then run any engine
console command from another machine with the shipped client:

```
python scripts/rcon.py --host <server ip>            # interactive console
python scripts/rcon.py --host <server ip> -c "LoadLevel Levels/Arena2/Arena2.spawnable"
```

Authentication is HMAC-SHA256 challenge-response — the password never
crosses the wire. Alternatively administer over plain SSH (run the server
in `tmux` and attach), or tunnel the rcon port through SSH for full
encryption; see `Gems/ServerAdmin/README.md`.

---

## 17. Secure networking (anti-tamper)

The engine's UDP transport ships DTLS support with the cipher suite
`ECDHE-RSA-AES256-GCM-SHA384`. Once enabled you get, per connection:

| Property | Mechanism |
| --- | --- |
| Server identity (anti-MITM) | **RSA-authenticated handshake** — the server proves possession of its RSA private key by signing the key exchange; clients verify against the certificate, optionally **pinned** (`net_SslEnablePinning`) |
| Fresh session keys / forward secrecy | ECDHE — a new ephemeral key per session; recorded traffic can't be decrypted later even if the RSA key leaks |
| Confidentiality + integrity | AES-256-GCM AEAD on every packet — a single flipped bit fails the auth tag and the packet is dropped |
| Replay resistance | DTLS sequence numbers + replay window; handshake cookies rotate (`net_RotateCookieTimer`) |

### 17.1 Setup

1. **Generate the server's RSA key + certificate** (helper included):

   ```bash
   python scripts/generate_network_certs.py --out certs/
   # options: --cn <name> --days <validity> --bits <RSA size>
   ```

   It creates `serverkey.pem` (private key — **server only, never commit,
   never distribute**) and `servercert.pem` (public — ships with clients for
   pinning). It refuses to overwrite existing keys.

2. **Server cfg** additions:

   ```text
   net_UdpUseEncryption true
   net_SslExternalCertificateFile Certificates/servercert.pem
   net_SslExternalPrivateKeyFile Certificates/serverkey.pem
   ```

3. **Client cfg** additions (cert only — no private key):

   ```text
   net_UdpUseEncryption true
   net_SslExternalCertificateFile Certificates/servercert.pem
   ```

   Cert paths resolve relative to the asset products folder (`@products@`).

### 17.2 Hardening cvars

| CVar | Default | Meaning |
| --- | --- | --- |
| `net_UdpUseEncryption` | false | master switch for DTLS on UDP connections |
| `net_SslEnablePinning` | true | remote cert must exactly match the local copy |
| `net_SslValidateExpiry` | true | reject expired certificates |
| `net_SslAllowSelfSigned` | true | accept self-signed certs that are otherwise trusted (fine with pinning; use a CA for storefront distribution) |
| `net_SslCertCiphers` | `ECDHE-RSA-AES256-GCM-SHA384` | cipher suite |
| `net_SslMaxCertDepth` | 3 | max cert chain depth |
| `net_RotateCookieTimer` | 50 ms | DTLS handshake cookie rotation |

### 17.3 Threat model (honest version)

| Threat | Stopped by |
| --- | --- |
| Reading traffic (sniffing) | AES-GCM encryption |
| Modifying packets in flight | GCM auth tag (packet dropped) |
| Replaying captured packets | DTLS replay window |
| Impersonating the server (MITM) | RSA cert verification + pinning |
| Decrypting recorded traffic later | ECDHE forward secrecy |
| **Malicious client sending valid-but-dishonest data** | **NOT crypto — server authority (§15.4/§16)** |

Key management rules: private key exists only on the server; never commit
keys or certs with keys to source control; rotate periodically (regenerate +
redistribute the public cert); use a real CA if you can't ship the pinned
cert with clients.

### 17.4 Protecting the client from the server

Everything above protects the *server* from clients. The reverse direction
matters too: a client trusts the server it connects to, and a server that is
malicious (or taken over — e.g. leaked rcon/SSH credentials) should not be able
to make clients do arbitrary things.

Upstream O3DE lets the server push console commands to clients (cvar sync on
connect plus the `ConsoleCommand` packet), historically with **no restriction on
which command** — including console *functions* that load levels, asset bundles,
audio files or Lua scripts from the client's disk. This fork restricts it:

| CVar | Default | Meaning |
| --- | --- | --- |
| `cl_serverConsoleCommandPolicy` | 1 | 0 = ignore every console command the server sends; 1 = accept **cvar assignments only** (console functions are rejected and logged); 2 = legacy upstream behavior, accept anything |

Under the default policy a server can still tune replicated gameplay cvars —
which is what the mechanism exists for — but cannot invoke `LoadLevel`,
`loadbundles`, `ExecuteLuaScript`, `s_PlayFile` or any other function on a
client. Rejections are logged with the command name, so a server trying it is
visible in the client log. Set it to `0` when connecting to servers you don't
control at all.

Other client-side rules the fork applies to server-supplied data:

- **Server browser**: addresses from the master server are validated as plain
  host/IP literals (and names/maps truncated, list length capped) before they
  are shown or used to build the `connect` command.
- **Voice relay**: the client only accepts voice datagrams from the relay
  address it joined, rejects packets larger than one audio frame, and caps the
  number of concurrent talker playback objects.
- **rcon**: admin commands execute only on the server that authenticated them;
  there is no path from the admin channel to a connected client's console.

Residual risk (be honest about it): the engine is C++ and asset/media parsers
are memory-unsafe, so a hostile server can still attack a client through the
data it sends. Server-side, run the dedicated server as an unprivileged user;
client-side, only connect to servers you have some reason to trust.

### 17.5 Randomized-program attestation + proof-of-work

On top of the HMAC audit challenges (Network Arena Audit component), the fork
ships two Monero-inspired hardening layers, both configurable on the same
component:

**Attestation (RandomX-style).** Every Nth audit challenge carries a random
64-bit seed. Both server and client deterministically generate the same random
program from the seed (register mixing + data-dependent reads) and execute it
over a shared reference dataset (`ArenaAttestDataset`: baked gameplay constants
plus anything the game appends — e.g. the bytes of critical Lua scripts). The
client's digest is HMAC-bound into its audit response and must match the
server's own execution. Because the program differs every challenge, answers
cannot be precomputed; a client whose dataset content was patched fails.

**Proof-of-work at spawn.** The server issues a memory-hard Hashcash-style
challenge (sequential SHA-256 memory fill + data-dependent random walk); the
client solves it on a background thread and must submit a valid nonce before
the deadline. Verification on the server is a single evaluation. This makes
throwaway accounts and kick-reconnect loops computationally costly. Difficulty
doubles per bit; the client clamps server-requested parameters to sane bounds.

| Property | Default | Meaning |
| --- | --- | --- |
| `AttestEveryNChallenges` | 2 | attach an attestation program to every Nth audit challenge (0 disables) |
| `AttestOpCount` | 8192 | operations per attestation program |
| `PowRequired` | true | require the proof-of-work after spawn |
| `PowMemoryKib` | 1024 | memory-hard buffer size (KiB) |
| `PowPasses` | 2 | random-walk passes over the buffer |
| `PowDifficultyBits` | 10 | leading zero bits required (expected work ~2^bits evaluations) |
| `PowDeadline` | 60 s | seconds to submit a valid nonce before it counts as a strike |

Honest limits: attestation detects tampered *registered content* and raises
the cost of emulated clients (they must execute arbitrary random programs
within the response deadline); it cannot prove the whole process is unmodified
— a cheat can keep pristine copies of the dataset. Proof-of-work throttles
abuse, it does not identify cheaters. Prevention remains the
server-authoritative simulation. The portable core lives in
`AzCore/Math/Attestation.h` (`AZ::Attestation::ExecuteProgram`,
`SolvePow`/`VerifyPow`) if you want to reuse it elsewhere (e.g. a PoW gate on
the master-server list API).

Full guide: `docs/aio3de/SECURE_NETWORKING.md`.

---

## 18. Match flow, weapons and audio

These live in `ArenaShooterNet` (server-authoritative) plus kit scripts in
`ArenaShooter`.

### 18.1 Match flow (Network Arena Match)

Add the **Network Arena Match** component to a network-bound level entity. The
server owns the phase machine and replicates it, so every client's HUD agrees:

```text
Warm-up ──(WarmupSeconds)──▶ Live ──(ScoreLimit or TimeLimit)──▶ Intermission
   ▲                                                                  │
   └──────────────── map vote resolves, LoadLevel ────────────────────┘
```

| Property | Meaning |
| --- | --- |
| `WarmupSeconds` | free-roam period before the round starts; combat disabled |
| `TimeLimit` / `ScoreLimit` | win conditions, whichever hits first |
| `IntermissionSeconds` | end-of-match downtime during which the vote runs |
| `MapList` | comma-separated level names offered in the vote |

During intermission players vote with **1–4** or the gamepad **d-pad**; the
winning level is loaded with a `LoadLevel` on the server, which cycles every
connected client. Replicated phase, remaining time, scores and vote tallies are
available to the HUD scripts. Warm-up and intermission are also the natural
windows for changing settings or keybinds (§19.1) — those never require a
reconnect.

### 18.2 Weapons

Weapons are configured on the networked player as a single string, so the
server is the only source of weapon stats:

```text
Rifle,12,0.15,200|Shotgun,70,0.9,25|Sniper,90,1.4,500
     ^  ^    ^    ^
     |  |    |    └ range (m)
     |  |    └ minimum interval between shots (s)
     |  └ damage
     └ display name
```

The client sends only the *slot index* it wants; damage, fire rate and range
always come from the server's config, and the server rejects shots that arrive
faster than the configured interval. Switching works while alive, including
mid-match downtime:

| Input | Action |
| --- | --- |
| Mouse **scroll wheel** | next / previous weapon |
| **E** / **Q** | next / previous weapon |
| Gamepad **RB** / **LB** | next / previous weapon |

Offline play gets the same behavior from `WeaponSwitcher.lua` driving
`Weapon.lua`'s per-weapon parameters.

### 18.3 Death animation

`NetworkArenaHealthComponent` replicates an `IsDead` flag; the body stays in
place while dead instead of teleporting. `DeathFx.lua` maps the flag onto an
anim-graph `Dead` parameter (add a death state/transition in the anim graph —
see upstream EMotionFX docs) and optionally triggers a death sound.

### 18.4 Spatial and stereo audio

Weapon fire, weapon switch and death sounds are raised as gameplay events that
kit scripts turn into MiniAudio playback:

- **Positional (3D)**: enable spatialization on the sound component and put a
  listener on the camera — you get distance attenuation and panning.
- **Stereo/flat**: leave spatialization off for UI and music.
- **Independent volume**: each sound source carries its own volume; concurrent
  sources never sum into a louder mix. The same rule is applied per remote
  talker in voice chat (§19.3).

---

## 19. Settings, server browser and voice chat

### 19.1 GameSettings: FOV, sensitivity, keybinds

The **GameSettings** gem persists per-user settings to `gamesettings.json` in
the project's user folder and exposes them on a Lua/Script Canvas bus. Its
**Remappable Input** component performs runtime rebinding: it captures the next
key pressed and applies the new binding immediately — no restart, no
reconnect, so it works mid-match during warm-up or intermission.

`SettingsMenu.lua` in the kit is the ready-made UI: **F10** / gamepad **Start**
opens it, arrows or d-pad navigate, left/right adjust **FOV** and
**mouse/gamepad sensitivity**, Enter/A starts a rebind capture. All changes
apply instantly and survive restarts.

### 19.2 ServerBrowser: discovery and joining

Two halves:

1. **Listing service** — `scripts/master_server.py`, stdlib-only, runs on any
   VPS: servers `POST /announce` heartbeats, clients `GET /servers`, entries
   expire after `--ttl` seconds without a heartbeat.
2. **Gem** — dedicated servers announce with `sb_announce true` +
   `sb_master_url` (reporting `sb_server_name`, `sb_game_port`, `sb_map`,
   `sb_players`, `sb_max_players`, all settable from game logic or rcon);
   clients call `RefreshServerList()` / `JoinServer(address, port)`.

`ServerBrowserMenu.lua`: **F9** / gamepad **Back** opens and refreshes the
list, up/down selects, Enter/A joins. Direct `connect <ip>:<port>` remains as
a fallback. Addresses coming from the master server are validated before use
(§17.4) — the master server is untrusted input.

### 19.3 VoiceChat: team voice

| Piece | Behavior |
| --- | --- |
| Capture | push-to-talk opens a Microphone-gem session; 20 ms mono 16 kHz frames, optional RMS gate (`voice_vad_threshold`) |
| Compression | G.711 mu-law, ~16 kB/s upstream while talking |
| Transport | plain UDP to a relay hosted by the dedicated server (`voice_host true`, `voice_port`, default **33452** — forward it alongside the game port) |
| Routing | the relay forwards audio only to clients on the **same channel**; set the channel to the team id for team-only voice |
| Playback | one ring buffer + sound per remote talker through MiniAudio, each with independent volume; `OnTalkerActive` drives HUD speaker indicators |

`VoiceChat.lua`: hold **V** / d-pad **up** to talk, **M** / d-pad **down**
mutes incoming voice; set the relay address, port, team channel and volume as
properties.

The voice stream is a separate plain UDP channel and is **not** covered by the
game's DTLS transport — it carries audio only. Both ends validate what they
receive (frame-size bounds, relay-address check, talker cap); for
confidentiality, tunnel it (VPN/WireGuard).

### 19.4 Remote administration recap

Server operators get the rcon channel from §16 (`admin_enable`,
`admin_password`, `scripts/rcon.py`), which can drive all of the above at
runtime: change maps, adjust match cvars, update the announced player count,
kick players. Admin commands execute only on the server that authenticated
them.

---

## 20. Troubleshooting

### 20.1 MSVC out of heap (C1060)

`error C1060: compiler is out of heap space` — the compiler ran out of
virtual memory. In order of effectiveness:

1. **Increase the Windows page file**: System → Advanced system settings →
   Performance Settings → Advanced → Virtual memory → Change → untick
   "Automatically manage", set Custom 16384–32768 MB → reboot. This is the
   real fix.
2. Build serially: `-- /m:1 /p:CL_MPCount=1` (`/m` limits parallel *projects*;
   `CL_MPCount` limits parallel compiles *within* a project — you need both).
3. Close other memory-heavy apps during the build.

### 20.2 PDB errors (C1090 / C1033)

`PDB API call failed` / `Cannot open program database` after a crashed build:
the target's PDB is corrupt or locked.

1. `taskkill /f /im mspdbsrv.exe` (kills the PDB server holding the file).
2. Delete the target's intermediate dir, e.g.
   `build\windows\Code\Framework\AzToolsFramework\CMakeFiles\AzToolsFramework.dir`.
3. **Re-run the CMake configure** before building — deleting the intermediates
   also deletes CMake-generated `unity_*.cxx` files, and MSBuild will not
   regenerate them (you'd hit C1083 `Cannot open source file: unity_*.cxx`).
4. Rebuild.

### 20.3 Editor "freezes" before showing a window

Almost always the **Asset Processor** reprocessing after a rebuild — check the
AP tray icon's job count and let it finish (minutes, first time). If truly
stuck >15 min with the AP idle: quit both, start AssetProcessor manually, let
it settle, then start the Editor. Check
`<project>\user\log\Editor.log` — the "Built on" date at the top tells you
whether you're even running the new build, and the last lines show where it
stalled.

### 20.4 A gem's component doesn't appear in Add Component

The Editor binary you're running doesn't contain the gem. Verify, in order:

1. `<project>/project.json` → `gem_names` contains the gem.
2. You **re-ran the CMake configure** after enabling it.
3. You rebuilt the Editor and the build output showed the gem's targets
   compiling.
4. You relaunched the rebuilt Editor.

### 20.5 Particles invisible / no `.particle` assets

Stale Asset Processor: it must load the gem's asset builder. Fully quit the AP
(tray → Quit) and the Editor, relaunch the Editor (spawns a fresh AP), let it
finish processing `Gems/OpenParticleSystem/Assets/...`, then search `firework`
in the Asset Browser. Check the Editor console for red `ParticleSystem` lines.

### 20.6 Bot doesn't move / doesn't shoot

- The entity needs a PhysX **Character Controller** (movement goes through it).
- The bot only fires with line of sight — check nothing blocks the ray from
  its eye height.
- Model file set but heuristic behavior + a console warning → the
  `.weights.json` widths don't match 8-in/5-out; fix the model spec.

### 20.7 Multiplayer client can't connect

- Test locally first (`connect 127.0.0.1:33450`).
- Server actually hosting? (`host` in its console/cfg; check its log.)
- Port/protocol: the transport is **UDP**; forward the UDP port and verify
  with an online port checker before remote tests.
- With encryption on: both sides need `net_UdpUseEncryption true` and the same
  cert (pinning rejects mismatches); check the logs for SSL validation errors.

### 20.8 Vulkan cannot present / Editor shows a "cannot present" error

An error like `No Vulkan queue on device ... supports presenting to the
display surface` means the GPU driver cannot put frames on your window.
Common causes, in order:

1. **Wrong/missing GPU driver** — Vulkan fell back to CPU rendering
   (`llvmpipe`) or the wrong device. Check `vulkaninfo --summary` (package
   `vulkan-tools`). NVIDIA: `nvidia-smi` must work; if not, install the
   driver (`sudo ubuntu-drivers autoinstall`). Under Secure Boot the NVIDIA
   module must be MOK-signed — complete the blue "MOK Management" enrollment
   on reboot, or disable Secure Boot. AMD/Intel: `sudo apt install
   mesa-vulkan-drivers`.
2. **Wayland session** — log out and pick **"Ubuntu on Xorg"** at the login
   screen (gear icon).
3. **No DRI3 in Xorg** (`No DRI3 support detected` in the terminal) — enable
   it in `/etc/X11/xorg.conf.d/` (`Option "DRI" "3"` on the device section)
   and re-login.
4. **Remote desktop** (VNC/xrdp/ssh -X) — these X servers cannot present
   Vulkan; use the machine's real display or a GPU-accelerated streaming
   solution.

Also check the monitor is plugged into the GPU you expect (hybrid systems
render fastest when the displaying GPU is the rendering GPU).

### 20.9 Where the logs are

- Editor: `<project>\user\log\Editor.log`
- Launchers: `<project>\user\log\Game.log` / server log next to it
- Asset Processor: its own window/GUI shows per-asset job results

---

## 21. Reference tables

### 21.1 Rendering cvars

| CVar | Purpose |
| --- | --- |
| `r_rayTracedShadows` | toggle ray-traced shadows |
| `r_rayTracedShadowsRebuild` / `...AutoRebuild` / `...AutoRebuildPollFrames` | BVH rebuild control |
| `r_rayTracedShadowsPrewarm` | build BVH at level load |
| `r_rayTracedShadowsBias` / `...Factor` / `...MaxDistance` / `...MaxTriangles` | quality/budget |
| `r_useBatchedSphereCulling` / `r_batchedSphereCullingMinEntries` | SIMD CPU culling |
| `r_useEntryWorkListsForCulling` / `r_numEntriesPerCullingJob` / `r_numNodesPerCullingJob` | culling job granularity |
| `r_debugTerrainLodLevels` / `r_debugTerrainAabbs` | terrain LOD debug views |

### 21.2 Networking & security cvars

| CVar | Purpose |
| --- | --- |
| `host` / `connect <ip>:<port>` / `disconnect` | session control (console commands) |
| `sv_port` | server listen port |
| `net_UdpUseEncryption` | enable DTLS |
| `net_SslExternalCertificateFile` / `net_SslExternalPrivateKeyFile` | cert + key (key: server only) |
| `net_SslEnablePinning` / `net_SslValidateExpiry` / `net_SslAllowSelfSigned` | validation policy |
| `net_SslCertCiphers` / `net_SslMaxCertDepth` / `net_RotateCookieTimer` | suite/chain/cookie tuning |
| `cl_serverConsoleCommandPolicy` | which console commands a server may run on this client (0 none / 1 cvars only / 2 legacy) |

### 21.3 Game systems cvars

| CVar | Gem | Purpose |
| --- | --- | --- |
| `admin_enable` / `admin_password` / `admin_port` | ServerAdmin | rcon channel on the dedicated server |
| `sb_announce` / `sb_master_url` | ServerBrowser | announce this server to the master list |
| `sb_server_name` / `sb_game_port` / `sb_map` / `sb_players` / `sb_max_players` | ServerBrowser | what the announcement reports |
| `voice_host` / `voice_port` | VoiceChat | host the voice relay (server side) |
| `voice_vad_threshold` | VoiceChat | RMS gate while talking (0 = send everything) |

### 21.4 Gameplay events (ArenaShooter contract)

| Event | Payload | Channel | Raised by | Handled by |
| --- | --- | --- | --- | --- |
| `Damage` | float amount | damaged entity | Weapon.lua, Bot Agent, server shot resolution | Health.lua, Network Arena Health |
| `Killed` | float | dying entity | Health.lua | ScoreHud.lua, your scripts |
| `Respawned` | float | respawned entity | Health.lua | your scripts |
| `MoveSpeed` | float m/s | player entity | PlayerController.lua | AnimationDriver.lua |
| `ActiveInputDevice` | 1.0 / 2.0 | detector entity | DeviceDetector.lua | UI/prompt scripts |

### 21.5 Neural bot policy I/O

| # | Input (8) | # | Output (5) |
| --- | --- | --- | --- |
| 0–2 | target dir, bot-local x/y/z | 0 | strafe (−1..1) |
| 3 | distance / 50 | 1 | forward (−1..1) |
| 4 | line of sight (0/1) | 2 | turn (−1..1 × max turn speed) |
| 5 | own speed / 10 | 3 | shoot (>0.5 fires) |
| 6 | cos(angle to target) | 4 | jump (reserved) |
| 7 | time since seen / 2 | | |

---

## 22. Document index

Deeper, per-topic documents in this repository:

| Document | Topic |
| --- | --- |
| `docs/aio3de/QUICKSTART.md` | shortest path to a running Editor |
| `docs/aio3de/BUILDING_LINUX.md` | Linux build specifics |
| `docs/aio3de/GETTING_STARTED_TUTORIAL.md` | first playable level, step by step |
| `docs/aio3de/SCRIPTING_AND_GAME_LOGIC.md` | Lua/Script Canvas recipes |
| `docs/aio3de/RAY_TRACING.md` | BVH + ray-traced shadows internals |
| `docs/aio3de/GPU_CULLING.md` | GPU-driven culling passes |
| `docs/aio3de/SECURE_NETWORKING.md` | full transport-security guide |
| `docs/aio3de/ENGINE_ANALYSIS.md` | fork engine analysis |
| `Gems/ArenaShooter/README.md` | offline game kit |
| `Gems/NeuralBots/README.md` | bot agent + training |
| `Gems/ArenaShooterNet/README.md` | multiplayer setup |
| `Gems/AIBackbone/README.md` | AI model builder & training tools |
| `Gems/ServerAdmin/README.md` | rcon setup + SSH guidance |
| `Gems/GameSettings/README.md` | settings persistence & rebinding |
| `Gems/ServerBrowser/README.md` | master server + browser setup |
| `Gems/VoiceChat/README.md` | voice chat setup & security notes |
| `Gems/CSharpScripting/README.md` | C# scripting setup + API reference |
| `Gems/LLMAssist/README.md` | in-Editor AI assistant setup |

For everything the fork does **not** change, use the upstream O3DE docs — see
[§1.1](#11-upstream-o3de-documentation).
