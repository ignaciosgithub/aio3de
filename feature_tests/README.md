# aio3de feature tests

A self-contained harness to judge the **functionality** and **performance** of the
engine features this fork adds on top of stock O3DE. Run it after any engine
`git pull` + rebuild to confirm the features still pass and to measure how they
perform on *your* hardware.

It covers:

| Feature | PR | What the harness checks |
| --- | --- | --- |
| SoA + SIMD batched frustum culling (`AZ::FrustumClassifySpheres`) | #4 | correctness vs scalar reference + scalar/SIMD/parallel throughput |
| `AZ::ParallelFor` TaskGraph primitive | #2 | correctness (each index once, empty/single ranges) + serial vs parallel throughput |
| Portable ray-tracing BVH core | #8 | build/closest-hit/any-hit correctness + build & traversal throughput |
| Ray-traced hard shadows (CPU reference) | #14 | occlusion/max-distance/normal-bias correctness, batch-vs-brute-force + shadow-batch throughput |

These are all **CPU** checks and micro-benchmarks — they run headless, need no
GPU/display, and finish in a couple of minutes. The GPU-driven variants of these
features (GPU culling, the ray-traced-shadows compute pass) are exercised in the
Editor; see [GPU features](#gpu-driven-features-editor) below.

## Prerequisites

Build the test runner + AzCore test library once (fast, incremental after the
first full engine build):

```bat
:: Windows
cmake --build C:\Users\isavi\aio3detest\build\windows --target AzTestRunner AzCore.Tests --config profile -- /m:2
```
```bash
# Linux
cmake --build ~/aio3de/build/linux --target AzTestRunner AzCore.Tests --config profile
```

## Run

```bat
:: Windows  ->  run_all.bat [BUILD_DIR] [CONFIG]
feature_tests\run_all.bat
:: or point it at a specific build tree / config:
feature_tests\run_all.bat C:\Users\isavi\aio3detest\build\windows profile
```
```bash
# Linux  ->  run_all.sh [BUILD_DIR] [CONFIG]
./feature_tests/run_all.sh
```

Defaults: `BUILD_DIR` = `C:\Users\isavi\aio3detest\build\windows` on Windows /
`<engine>/build/linux` on Linux; `CONFIG` = `profile`. Override the build tree
without arguments by setting the `AIO3DE_BUILD_DIR` environment variable.

Each run prints a summary and writes a full timestamped report to
`feature_tests/results/report_<timestamp>.txt` (that folder is git-ignored). The
process exit code is the number of failed functionality suites (0 = all passed),
so you can gate a script on it.

## Reading the results

- **Functionality**: each suite prints `-> PASSED` / `-> FAILED`. A FAIL means an
  engine change broke that feature's correctness — the report contains the full
  gtest output with the failing assertion.
- **Performance**: benchmark rows show wall time per iteration and, where set,
  `items_per_second`. To judge whether a feature actually helps, compare the
  paired rows at the **same size**, e.g.:
  - `BM_FrustumCull/BatchedSimd/1048576` vs `BM_FrustumCull/Scalar/1048576`
    → the SIMD speedup over the scalar path.
  - `BM_FrustumCull/BatchedSimdParallel/*` → adds multi-threading on top.
  - `ParallelForBenchmarkFixture/IntegrateParallel/*` vs `.../IntegrateSerial/*`
    → ParallelFor scaling.
  - `BM_RayTracingBvh/IntersectAny/*` vs `IntersectClosest/*` → the any-hit
    early-out saving that shadow rays rely on.

  To track a change across engine pulls, keep the report files — they're
  timestamped — and diff the same benchmark row before/after.

## Adding a new feature to the harness

1. Add a gtest suite (functionality) and, guarded by `#if defined(HAVE_BENCHMARK)`,
   a Google-Benchmark fixture (performance) under `Code/Framework/AzCore/Tests/...`,
   and register the file in `azcoretests_files.cmake`.
2. Add a `run_functional "<name>" "<gtest_filter>"` line and, if you added a
   benchmark, extend the `run_bench` filter, in **both** `run_all.bat` and
   `run_all.sh`.

The runner drives `AzTestRunner`:
`AzTestRunner <AzCore.Tests lib> AzRunUnitTests --gtest_filter=...` for
functionality and `... AzRunBenchmarks --benchmark_filter=...` for benchmarks.
(In benchmark mode `AzTestRunner` prints its results and returns 0, then aborts
during static teardown — that's a harmless known quirk; the numbers above the
abort are valid, and the harness ignores that teardown status.)

## GPU-driven features (Editor)

The GPU variants need a real device, so they're validated in the Editor rather
than this headless harness (which verifies their CPU reference and that the
shader/pass are registered). All are **opt-in** — registered with the pass system
but in no default pipeline, so they never affect a project until you enable them.

- **GPU-driven frustum culling** (#7/#9): passes `GpuFrustumCullPass` /
  `GpuFrustumCullDrawPass`, gated by the console variable `r_useGpuDrivenCulling`.
  Toggle it from the Editor console (`~`) with `r_useGpuDrivenCulling 1`. To wire
  the passes into a pipeline, add them to your render pipeline's `.pass` template
  (see `Gems/Atom/Feature/Common/Assets/Passes/GpuFrustumCull*.pass`).
- **Ray-traced BVH traversal** (#8): pass `RayTracingBvhPass`
  (`Assets/Passes/RayTracingBvhTraverse.pass`).
- **Ray-traced hard shadows** (#14): pass `RayTracedShadowsPass`
  (`Assets/Passes/RayTracedShadows.pass`, shader
  `Assets/Shaders/RayTracing/RayTracedShadows.azsl`). The GPU shader mirrors the
  CPU reference this harness verifies, one occlusion ray per surface sample.
- **Fullscreen ray-traced shadows** (live-toggleable): pass
  `RayTracedShadowsFullscreenPass`
  (`Assets/Passes/RayTracedShadowsFullscreen.pass`, shader
  `Assets/Shaders/RayTracing/RayTracedShadowsFullscreen.azsl`). Wired into the
  main pipeline **disabled by default** and driven entirely by cvars — no JSON
  editing needed. See the live A/B workflow below.

To measure GPU-side cost, use the Editor's built-in profiler (**Tools → Profiler**,
or the ImGui `r_ProfilerSystem` overlay) and compare frame/pass timings with the
feature's cvar on vs off. GPU frame timing must be read on real hardware — that's
why it's not part of the CPU harness.

## Live A/B testing GPU features in the Editor

The fullscreen ray-traced shadows pass is built for real-time comparison: it sits
in the main pipeline disabled, and a feature processor flips it on/off at runtime
from a console variable, feeding it the actual scene geometry and directional
light.

Cvars (all live; type in the Editor console `~` while in game mode `Ctrl+G`):

| Cvar | Default | Meaning |
|------|---------|---------|
| `r_rayTracedShadows` | `false` | Master toggle for the fullscreen RT shadows pass |
| `r_rayTracedShadowsFactor` | `0.25` | Brightness multiplier for shadowed pixels (0 = black, 1 = invisible) |
| `r_rayTracedShadowsMaxDistance` | `10000` | Max occlusion-ray distance (meters) |
| `r_rayTracedShadowsBias` | `0.02` | Ray-origin offset to avoid self-shadow acne (meters) |
| `r_rayTracedShadowsMaxTriangles` | `1000000` | Cap on scene triangles gathered into the occluder BVH |
| `r_rayTracedShadowsRebuild` | `false` | Set `true` once to force a BVH rebuild (e.g. after only *moving* meshes) |
| `r_rayTracedShadowsPrewarm` | `true` | Build the BVH in the background at level load so the first enable is instant |
| `r_rayTracedShadowsAutoRebuild` | `true` | Auto-rebuild the BVH (async) when meshes are added/removed |
| `r_rayTracedShadowsAutoRebuildPollFrames` | `30` | How often (frames) the auto-rebuild checks for mesh changes |

Measurement workflow (as close to a live A/B as the engine allows):

1. Open a level with some meshes and a directional light, enter game mode
   (`Ctrl+G`).
2. Disable VSync so you can see real frame-time deltas: `vsync_interval 0`
   (and `r_displayInfo 1` for the frame-time overlay). Let the scene warm up
   ~30 s so shader/PSO compilation spikes settle.
3. Open the GPU profiler: press **Home**, then **Atom Tools → Gpu Profiler**.
   Note the baseline frame time; `RayTracedShadowsFullscreenPass` is absent.
4. Toggle the feature on: `r_rayTracedShadows 1`. The scene geometry is gathered
   into a BVH on a background job (no frame hitch; with prewarm on it is
   already built at level load), shadows appear as darkened geometry opposite
   the light, and the pass shows up in the Gpu Profiler with its per-frame GPU
   cost.
5. Compare average (not single-frame) timings on vs off; flip the cvar live as
   often as you like. `r_rayTracedShadowsFactor 0.0` makes shadows fully black
   for maximum visual contrast.
6. Adding/removing meshes rebuilds the occluder BVH automatically (async, no
   hitch). If you only *move* meshes, `r_rayTracedShadowsRebuild true`
   refreshes it manually.

The shadows are hard (binary lit/shadowed, no penumbra) by design — they mirror
the CPU reference validated by this harness, and run on any GPU/backend (no
DXR / RT cores required).

## PSO precaching (first-use frame spikes)

Pipeline State Objects (PSOs) are compiled by the driver the first time a
shader/state combination is drawn — the source of the "first time dips, then
cached" pattern (new view angles, first feature toggles). The engine persists
those compiled PSOs to disk (per shader "pipeline library") and reloads them at
startup, so from the second run onward they're precompiled at load:

| Cvar / command | Default | Meaning |
|------|---------|---------|
| `r_enablePsoCaching` | `true` | Persist compiled PSOs to disk at shutdown and reload them at startup |
| `r_savePsoCache` | (command) | Save all live shaders' PSO caches to disk right now |
| `r_psoCacheAutoSaveIntervalSeconds` | `0` | If > 0, auto-save the PSO caches every N seconds (crash-proofing) |

To measure: run a session, move the camera around aggressively (compiling PSOs),
exit cleanly (or `r_savePsoCache`), relaunch, and repeat the same movements — the
first-use spikes from the first session should be gone.
