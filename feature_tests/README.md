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

To measure GPU-side cost, use the Editor's built-in profiler (**Tools → Profiler**,
or the ImGui `r_ProfilerSystem` overlay) and compare frame/pass timings with the
feature's cvar on vs off. GPU frame timing must be read on real hardware — that's
why it's not part of the CPU harness.
