# Getting started: your first level, step by step

This tutorial picks up where [`QUICKSTART.md`](QUICKSTART.md) leaves off. You
have an engine built, a project created, and the Editor launches. Now you'll
build a small playable scene: terrain, lighting, some physics props, a player
camera, and your first script.

If you haven't set up the engine and a project yet, do
[`QUICKSTART.md`](QUICKSTART.md) first.

Companion guide: [`SCRIPTING_AND_GAME_LOGIC.md`](SCRIPTING_AND_GAME_LOGIC.md)
covers Lua, Script Canvas, and reusable game-logic recipes in depth.

---

## 0. Launch order

1. Start the **Asset Processor** first (it also auto-starts with the Editor):
   `build/windows/bin/profile/AssetProcessor.exe` (Windows) or
   `build/linux/bin/profile/AssetProcessor` (Linux). On a fresh project it
   processes thousands of assets — let it settle before expecting the Editor
   to be responsive. The Editor waits for it, which can look like a freeze on
   first launch.
2. Start the **Editor** from the same folder and pick your project.
3. Create a new level: **File → New Level** (Ctrl+N).

## 1. Learn the camera and the core windows

- **Right mouse (hold) + WASD** — fly the viewport camera; **Q/E** down/up;
  mouse wheel changes fly speed.
- **Entity Outliner** (left) — every entity in the level.
- **Entity Inspector** (right) — components of the selected entity.
- **Asset Browser** (bottom) — all processed assets (meshes, materials,
  scripts, prefabs).
- **Console** (bottom) — warnings/errors and the `~` console for cvars in game
  mode.

The whole editing model is: **an entity is an empty container; components give
it behavior**. Everything below is "create entity → add components → tweak
properties".

## 2. Ground: either a terrain or a simple plane

### Option A — real terrain (recommended, exercises this fork's terrain work)

1. Level entity setup: your level needs the **Terrain World** components. Select
   the **Level** entity in the Outliner and add:
   - **Terrain World**
   - **Terrain World Renderer**
2. Create an entity (right-click in Outliner → *Create entity*), name it
   `Terrain`, and add:
   - **Axis Aligned Box Shape** — set its dimensions to e.g. `1024, 1024, 200`.
   - **Terrain Layer Spawner**
   - **Terrain Height Gradient List**
3. Create a second entity `TerrainHeight` with:
   - **FastNoise Gradient** (from the FastNoise gem; pick *Perlin*, tweak
     *Frequency* to ~`0.002`)
   - **Gradient Transform Modifier** and a **Shape Reference** pointing at the
     `Terrain` entity's box shape.
4. Back on `Terrain`, point the **Terrain Height Gradient List** at
   `TerrainHeight`. Hills appear.
5. Fork features to try on the **Terrain World Renderer** component (Mesh
   section):
   - **Auto LOD (screen-space error)** — on by default; sectors pick their LOD
     from measured geometric error. `r_debugTerrainLodLevels 1` in the console
     shows LOD colors.
   - **Terrain occlusion culling** — off by default; tick it so hills occlude
     objects behind them (cheaper frames on hilly scenes).

### Option B — a simple ground plane (fastest)

1. Create an entity `Ground`.
2. Add a **Mesh** component and pick a plane/box model from the Asset Browser
   (e.g. any `_box` or shader ball ground you have), or add a **Shape** +
   **White Box** component to block out geometry.
3. Scale it up with the transform (press **R** for scale).
4. Add a **PhysX Static Rigid Body** + **PhysX Collider** so things can land
   on it.

## 3. Light and sky

1. Create an entity `Sun`, add a **Directional Light** component, rotate it
   (press **E**) so it points down at an angle. Increase *Intensity* to taste.
2. Create an entity `Sky`:
   - **HDRi Skybox** with a cubemap from the Asset Browser (search `skybox`),
     or **Physical Sky** for a procedural sun/sky.
   - Add a **Global Skylight (IBL)** component for ambient lighting (use the
     same cubemap).
3. Optional but recommended: create `PostFX`, add a **PostFX Layer** +
   **Exposure Control** component; switch mode to *Manual* if you don't want
   auto-exposure shifting brightness when bright objects enter view.

## 4. Physics props

1. Create an entity `Crate`:
   - **Mesh** — pick any box-ish mesh.
   - **PhysX Dynamic Rigid Body**.
   - **PhysX Collider** — shape *Box*, sized to the mesh.
2. Move it a few meters above the ground (press **W** for move).
3. Duplicate it a few times (Ctrl+D) into a stack.
4. Press **Ctrl+G** to enter game mode: the crates fall and settle. **Esc**
   exits and restores the scene.

Fork extra: add a **Soft Body** component (Physics category, from the
SoftBodyPhysics gem) next to a Mesh component on a sphere, set *Pressure* to
`1`, and Ctrl+G — the mesh becomes a jiggly XPBD soft body. See the component's
Collision mode dropdown for interaction with the level and rigid bodies.

## 5. A controllable player camera

Quick version (fly camera):

1. Create an entity `Player`, place it above the ground.
2. Add a **Camera** component. Tick *Be this camera* on game mode start (or use
   the component's "View → Be this camera" button).
3. Enable the **Starting Point Input** and **Starting Point Movement** gems if
   they aren't already (`scripts/o3de enable-gem -gn StartingPointInput -pp <project>`,
   same for `StartingPointMovement`, then re-run CMake configure + build).
4. Add an **Input** component to `Player` and assign the default
   `player_controls` input bindings asset (or create your own `.inputbindings`
   with the Asset Editor: **Tools → Asset Editor → Input Bindings**; map
   `W/A/S/D` to events like `MoveForward`, `Strafe`).
5. Wire input to motion with a small Lua script — see
   [`SCRIPTING_AND_GAME_LOGIC.md`](SCRIPTING_AND_GAME_LOGIC.md#recipe-wasd-movement)
   for a copy-paste `PlayerMove.lua`, or use the ready-made scripts under
   `Gems/StartingPointMovement/Assets/Scripts/Components/` (add a **Lua Script**
   component and pick `MoveEntity.lua`).

Ctrl+G — you can move around your level.

## 5b. Particles (fire, smoke, sparks, rain)

The engine ships a full particle system: the **OpenParticleSystem** gem
(preview). Enable it for your project, re-run CMake configure, and rebuild:

```
scripts/o3de enable-gem -gn OpenParticleSystem -pp <project>
```

Then:

1. Create an entity, add the **Particle** component (Particle System category).
2. Assign a **Particle Asset** — start with a shipped sample from
   `Gems/OpenParticleSystem/Assets/Particles/` (`firework.particle`,
   `collision.particle`, `ribbon.particle`, `noise.particle`,
   `vortexForce.particle`, `mesh.particle`, ...). They appear in the Asset
   Browser once the gem is enabled.
3. To author your own: **Tools → (Preview) Particle Editor** (or the *Open in
   Particle Editor* button on the component). An effect is a set of
   **emitters**, each with *Spawn* modules (rate/burst, location shape, initial
   velocity/size/color/lifetime), *Update* modules (forces, drag, vortex,
   noise, color/size over life, collision, kill volumes, events), and a
   *Render* mode (camera-facing **sprite**, stretched **ribbon**/trails, or
   full **mesh** particles). Save the `.particle` asset and assign it.
4. Materials for particles live under
   `Gems/OpenParticleSystem/Assets/Materials/OpenParticle/` — duplicate one to
   change texture/blend mode (additive for fire/sparks, alpha for smoke).

## 6. Save, prefabs, and iteration

- **Ctrl+S** saves the level. Levels and everything in them are **prefabs**
  (`.prefab` files in your project folder — plain JSON, diff-friendly).
- Select several entities → right-click → **Create Prefab** to make a reusable
  piece (e.g. a lamp post with light + mesh + collider). Double-click a prefab
  instance to edit it in place; changes propagate to all instances.
- Everything you tweak in the Inspector while **not** in game mode persists.
  Changes made **during** game mode are reverted on Esc.

## 7. Running outside the Editor

- **Game launcher:** build the `<ProjectName>.GameLauncher` target and run it —
  it loads a level with `+LoadLevel <levelname>` on the command line.
- **Console/cvars:** `~` opens the console in game mode. Useful ones:
  `r_displayInfo 1` (fps overlay), `vsync_interval 0`, `sys_MaxFPS 150`,
  `r_debugTerrainLodLevels 1`, `physx_Debug 1` (collider wireframes).

## 8. Where to go next

- **Build a full game from an example kit:** enable the **ArenaShooter** gem
  (`scripts/o3de enable-gem -gn ArenaShooter -pp <project>`, asset-only — no
  rebuild) and follow
  [`Gems/ArenaShooter/README.md`](../../Gems/ArenaShooter/README.md): input
  bindings for keyboard/mouse + gamepad, player controller, hitscan weapon,
  health/respawn, score HUD, input device detection and an anim-graph driver.
- **Scripting and game logic:**
  [`SCRIPTING_AND_GAME_LOGIC.md`](SCRIPTING_AND_GAME_LOGIC.md) — Lua, Script
  Canvas, input, spawning, triggers, timers, and links to every sample script
  that ships in this repo.
- **Fork feature docs:** [`RAY_TRACING.md`](RAY_TRACING.md) (portable
  ray-traced shadows), [`GPU_CULLING.md`](GPU_CULLING.md) (GPU-driven culling),
  [`ENGINE_ANALYSIS.md`](ENGINE_ANALYSIS.md) (architecture tour).
- **Fork gems to explore:** `SoftBodyPhysics` (Soft Body component),
  `LevelStreaming` (Level Streaming component — grid-chunks your level and
  streams chunks in/out around the camera), `AIBackbone` (in-Editor AI model
  builder and ONNX import).
- **Upstream manual:** the O3DE docs at
  [o3de.org/docs](https://o3de.org/docs/welcome-guide/) apply to this fork for
  everything not listed above — component reference, materials, animation
  (EMotionFX), UI (LyShine), audio, networking.
