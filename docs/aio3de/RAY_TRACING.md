# Hardware-agnostic ray tracing (increment 1: portable BVH core + compute traversal)

O3DE's existing ray tracing (`RayTracingFeatureProcessor`) is **100% hardware-gated**: it queries
`RHISystemInterface::GetRayTracingSupport()` and, if no device exposes DXR / `VK_KHR_ray_tracing`,
sets `m_rayTracingEnabled = false` and *every* RT feature (reflections, RT shadows, debug RT)
silently bails. On GPUs without RT cores, on Metal, or on the Null backend you get no ray tracing
at all — there is no fallback.

This adds a **hardware-agnostic** path: a software BVH traversed in a plain compute shader, using
only standard compute + structured buffers, with **no** dependency on DXR / `VK_KHR_ray_tracing` or
RT cores. It runs on every RHI backend (Vulkan, DX12, Metal, Null) and any GPU. It reuses the exact
compute-pass + SoA-structured-buffer machinery from the GPU frustum-culling work (see
`GPU_CULLING.md`).

This is **increment 1** of a multi-PR effort: the portable core + the traversal shader + an opt-in
compute-pass scaffold. It is **not wired into any default pipeline**, so nothing changes by default.
Later increments wire it into a concrete effect (reflections / AO / hard shadows) and you profile on
real hardware.

## Components

| Layer | File | Role |
| --- | --- | --- |
| CPU core (reference) | `Code/Framework/AzCore/AzCore/Math/RayTracingBvh.{h,cpp}` | Builds a binary BVH over triangles; `IntersectClosest` / `IntersectAny`. |
| CPU tests | `Code/Framework/AzCore/Tests/Math/RayTracingBvhTests.cpp` | Unit tests incl. brute-force equivalence over random scenes. |
| GPU math | `Gems/Atom/Feature/Common/Assets/Shaders/RayTracing/RayTracingBvh.azsli` | Slab test + Möller–Trumbore, line-for-line mirror of the CPU core. |
| GPU traversal | `.../RayTracing/RayTracingBvhTraverse.azsl` + `.shader` | One thread per ray; stackless BVH walk; writes closest hit. |
| Pass template | `Gems/Atom/Feature/Common/Assets/Passes/RayTracingBvhTraverse.pass` | `RayTracingBvhTraverseTemplate`, 5 buffer slots. |
| Pass (C++) | `Gems/Atom/Feature/Common/Code/Source/RayTracing/RayTracingBvhPass.{h,cpp}` | `RPI::ComputePass`; builds the BVH, uploads buffers, dispatches rays. |

## Data flow

```
triangles (CPU) ── RayTracingBvh::Build ──► nodes[]  (BvhNodePacked, 32 B)
                                            orderedTriangles[]  ─► 3×float4 / tri
                                            primitiveIndices[]  (ordered slot → original id)
rays[] (origin, tMax, dir) ─────────────────────────────────────────────────┐
                                                                             ▼
   RayTracingBvhTraverse.azsl  [numthreads(64,1,1)]  one thread per ray
        walk BVH with a fixed stack[64]:
          - slab test  node.aabb vs ray            (IntersectRayAabb)
          - leaf: Möller–Trumbore each triangle    (IntersectRayTriangle)
          - keep closest t; primId = primitiveIndices[slot]
                                                                             ▼
   hits[]  (BvhRayHit: float t,u,v; uint primitiveId, hit)  ── consumed by the effect pass
```

## BVH node layout (CPU `BvhNodePacked` ≡ GPU `BvhNode`, 32 bytes)

```
float3 aabbMin;  uint leftFirst;     // interior: left child index (right = leftFirst+1)
float3 aabbMax;  uint triangleCount; // leaf: triangleCount>0, leftFirst = first ordered triangle
```

Children are stored **contiguously** (right child = `leftFirst + 1`), so the GPU walk needs only a
single child index per interior node. The builder is a deterministic midpoint split on the longest
centroid axis with a median fallback, so the CPU and GPU produce identical traversals.

## CPU/GPU correctness

The GPU is verified the same way as the GPU culling work: the CPU `RayTracingBvh` is the reference
and is fully unit-tested in the sandbox; the AZSL shader mirrors it line-for-line and is
`azslc`-verified.

- `RayTracingBvhTests.cpp` (6 tests, all passing headless):
  - empty BVH always misses;
  - single-triangle hit reports correct `t` + barycentrics;
  - ray miss / behind-origin / beyond-tMax all return no hit;
  - closest-of-stacked-triangles picks the nearest;
  - **`MatchesBruteForceOverRandomScene`**: 400 random triangles × 500 random rays × leaf sizes
    {1,2,4,8}, asserting BVH closest hit (`hit`, `t`, `primitiveId`) equals a brute-force linear
    scan, plus `IntersectAny` agreement and that primitive indices are a permutation;
  - child-contiguity + leaf-coverage invariants on the node array.
- `RayTracingBvh.azsli` `IntersectRayAabb` / `IntersectRayTriangle` are byte-for-byte the same math
  as `RayTracingBvhInternal::IntersectRayAabb` / `IntersectRayTriangle`; the `.azsl` traversal loop
  mirrors `RayTracingBvh::IntersectClosest` (same fixed `stack[64]`, same closest-keep rule).
- Shader compile-verified with the engine's `azslc` (`--semantic` clean, `numthreads(64,1,1)`, SRG
  layout parsed: 5 buffers + `m_rayCount` / `m_nodeCount`).

## Honest caveat (same as the GPU culling scaffold)

GPU frame-time cannot be measured in the headless software-rasterizer sandbox, so this is a
build-/correctness-verified scaffold. The CPU core *is* fully tested here; the GPU win is profiled
on real hardware.

## Wiring it into a pipeline (later increments)

1. Add `RayTracingBvhTraverseTemplate` as a pass to a render pipeline `.pass` (it is registered with
   the pass system as `RayTracingBvhPass` but is in no default pipeline).
2. From the consuming feature processor, feed geometry + rays:
   ```cpp
   auto* pass = /* find the RayTracingBvhPass instance */;
   pass->SetGeometry(triangles);          // builds the BVH and uploads node/triangle/prim buffers
   pass->SetRays(rays);                    // origin, tMax, direction per ray
   // after the dispatch, read pass->GetHitsBuffer() in the downstream effect pass
   ```
3. Build a consumer effect (e.g. compute-RT AO: cast a hemisphere of rays per pixel and accumulate
   `hit` results; or hard shadows: one occlusion ray per pixel toward the light using
   `IntersectAny`-style early-out).

## Profiling on real hardware

- Capture a frame in RenderDoc; confirm the `RayTracingBvhTraverse` compute dispatch executes on the
  target backend (Vulkan/DX12/Metal) with **no** ray-tracing-pipeline / acceleration-structure
  objects created — it is a plain compute dispatch, which is the whole point.
- Scale ray count and triangle count; watch the compute dispatch time. Compare against the
  hardware-RT path (where available) to quantify the portability-vs-throughput trade-off.

---

# Increment 2: hard ray-traced shadows (first concrete consumer)

Increment 1 shipped the portable BVH core + traversal. Increment 2 is the first **concrete effect**
built on it: **hard ray-traced shadows**. For each shaded surface sample it casts a single occlusion
ray toward the light and early-outs on the first occluder (`IntersectAny`), producing a binary
visibility (1 = lit, 0 = shadowed). Like everything above it is hardware-agnostic (plain compute +
structured buffers, no DXR / `VK_KHR_ray_tracing`), and **opt-in** — registered with the pass system
but in no default pipeline, so nothing changes by default.

## Components

| Layer | File | Role |
| --- | --- | --- |
| CPU core (reference) | `Code/Framework/AzCore/AzCore/Math/RayTracedShadows.{h,cpp}` | `ComputeShadowVisibility` (single sample) + `ComputeDirectionalShadowVisibility` (batch); offsets the ray origin by a normal bias and calls `RayTracingBvh::IntersectAny`. |
| CPU tests | `Code/Framework/AzCore/Tests/Math/RayTracedShadowsTests.cpp` | 5 tests incl. brute-force equivalence over a random scene. |
| GPU shader | `Gems/Atom/Feature/Common/Assets/Shaders/RayTracing/RayTracedShadows.azsl` + `.shader` | One thread per surface sample; any-hit BVH walk mirroring the CPU core; writes visibility. |
| Pass template | `Gems/Atom/Feature/Common/Assets/Passes/RayTracedShadows.pass` | `RayTracedShadowsTemplate`, 4 buffer slots. |
| Pass (C++) | `Gems/Atom/Feature/Common/Code/Source/RayTracing/RayTracedShadowsPass.{h,cpp}` | `RPI::ComputePass`; builds the BVH over occluders, uploads samples + params, dispatches one thread per sample, exports the visibility buffer. |

## Data flow

```
occluder triangles (CPU) ─ RayTracingBvh::Build ─► nodes[] + orderedTriangles[] (3×float4/tri)
surface samples[] (position, normal) ───────────────────────────────────────┐
ShadowRayParams { toLight, maxDistance, normalBias } ────────────────────────┤
                                                                             ▼
   RayTracedShadows.azsl  [numthreads(64,1,1)]  one thread per sample
        origin    = position + normal * normalBias      (avoid self-shadow acne)
        direction = normalize(toLight)
        occluded  = any-hit BVH walk in (0, maxDistance]   (early-out, IntersectAny)
                                                                             ▼
   visibility[]  (float: 1 = lit, 0 = shadowed)  ── consumed by the lighting pass
```

`maxDistance` is large for a directional light and the distance-to-light for a point/spot light;
`normalBias` offsets the origin off the surface so a sample cannot shadow itself.

## CPU/GPU correctness

Same discipline as increment 1: the CPU `RayTracedShadows` is the reference and is fully unit-tested
headless; the AZSL shader mirrors it and is `azslc`-verified.

- `RayTracedShadowsTests.cpp` (5 tests, all passing headless):
  - empty occluder scene is fully lit;
  - an occluder between surface and light casts a shadow (shadowed under, lit beside);
  - an occluder beyond `maxDistance` does **not** shadow;
  - `normalBias` prevents self-shadow acne on the occluder itself;
  - **`BatchMatchesBruteForceOcclusion`**: 600 samples × 40 random quads, asserting
    `ComputeDirectionalShadowVisibility` equals a brute-force linear-scan occlusion test.
- `RayTracedShadows.azsl`'s any-hit walk reuses `RayTracingBvh.azsli`'s `IntersectRayAabb` /
  `IntersectRayTriangle` and mirrors `RayTracingBvh::IntersectAny` (same fixed stack, same early-out).
- Shader compile-verified with the engine's `azslc` (`--semantic` clean, `numthreads(64,1,1)`, SRG
  layout parsed: 4 buffers + `m_toLight` / `m_maxDistance` / `m_normalBias` / `m_sampleCount` /
  `m_nodeCount`).

## Wiring it into a pipeline

1. Add `RayTracedShadowsTemplate` as a pass to a render pipeline `.pass` (registered as
   `RayTracedShadowsPass`, in no default pipeline).
2. From the consuming feature processor:
   ```cpp
   auto* pass = /* find the RayTracedShadowsPass instance */;
   pass->SetOccluderGeometry(occluderTriangles);   // builds the BVH + uploads node/triangle buffers
   pass->SetSamples(surfaceSamples);               // world position + normal per shaded sample
   pass->SetShadowParams({ toLight, maxDistance, normalBias });
   // after the dispatch, read pass->GetVisibilityBuffer() in the lighting pass
   ```
3. Multiply the light's contribution by the per-sample visibility in the lighting/composite pass.

## Honest caveat

Same as increment 1: GPU frame-time is profiled on real hardware; here the CPU reference is fully
tested and the shader is `azslc`-verified. Only hard (single-ray) shadows are implemented — soft
shadows / penumbra (multiple rays or cone sampling) and the silhouette-edge occluder optimization are
future increments.

---

# Increment 3: soft ray-traced shadows (penumbra + contact hardening)

The live fullscreen pass (`RayTracedShadowsFullscreenPass`, toggled with `r_rayTracedShadows`) now
casts **several rays per pixel spread over the light's disk** instead of one ray at its center. The
visibility is the fraction of rays that reach the light, so shadows get a penumbra whose width grows
with the distance between occluder and receiver: sharp where an object touches the ground, softer
further away (contact hardening). A filtered shadow map blurs by a constant amount; this does not.
Still hardware-agnostic: plain pixel shader + structured buffers, no DXR / `VK_KHR_ray_tracing`.

## How it works

- **Disk size** comes from the Directional Light component's existing **Angular diameter** (0.5° by
  default, about the sun's), or `r_rayTracedShadowsSoftness <degrees>` to override it (`0` = hard).
- **Sample pattern**: a Vogel (golden-angle) disk of `r_rayTracedShadowsSamples` points (default 4)
  mapped onto the light's cone around the light direction. Each pixel rotates the pattern by an
  interleaved-gradient-noise angle, turning banding into fine noise. With
  `r_rayTracedShadowsAnimateNoise true` the rotation also changes every frame so TAA averages more
  rays over time.
- **Adaptive early-out** (`r_rayTracedShadowsAdaptive`, default on): first cast
  `max(2, samples / 4)` probe rays: the outermost samples of the Vogel disk, which sit on its rim
  at well-spread angles. If they all hit or all miss, the pixel is
  in full shadow / full light and the rest are skipped; only pixels whose probes disagree (the
  penumbra) cast every ray. Most of the screen therefore pays 2 rays per pixel.
- **Composite**: the output factor is `lerp(r_rayTracedShadowsFactor, 1, visibility)`, multiplied
  onto the lighting target as before.
- `r_rayTracedShadowsSamples 1` (or softness 0) is exactly the previous hard-shadow path.

| Layer | File | Role |
| --- | --- | --- |
| CPU core (reference) | `Code/Framework/AzCore/AzCore/Math/RayTracedShadows.{h,cpp}` | `SoftShadowParams`, `GetSoftShadowProbeCount`, `ComputeSoftShadowRayDirection`, `ComputeSoftShadowVisibility`. |
| CPU tests | `Code/Framework/AzCore/Tests/Math/RayTracedShadowsTests.cpp` | 6 new tests (below). |
| CPU benchmark | `Code/Framework/AzCore/Tests/Math/RayTracingPerformanceTests.cpp` | `BM_RayTracingBvh/SoftShadowBatch/<tris>/<adaptive>`. |
| GPU shader | `Gems/Atom/Feature/Common/Assets/Shaders/RayTracing/RayTracedShadowsFullscreen.azsl` | Mirrors the CPU core: same basis, Vogel disk, probe order and early-out. |
| Pass / feature processor | `.../RayTracing/RayTracedShadowsFullscreenPass.{h,cpp}`, `RayTracedShadowsFeatureProcessor.cpp` | New SRG constants; reads the light's angular radius and the new cvars. |

## Verification

- New `MATH_RayTracedShadows` tests: probe count; every ray direction is normalized and inside the
  light cone (including a light pointing down -Z); 1 sample or 0 radius equals the hard shadow;
  across a straight shadow edge the penumbra is fractional and monotonic; the penumbra widens with
  occluder height (contact hardening); adaptive sampling equals full sampling everywhere outside the
  penumbra and differs on at most 2% of samples overall.
- CPU benchmark (`BM_RayTracingBvh/SoftShadowBatch`, 8 rays/sample, Linux VM): the adaptive
  early-out is 2.2x faster on 16K triangles and 1.5x on 131K triangles than always casting every
  ray. The GPU ratio depends on how much of the screen is penumbra.
- Shader: `azslc --semantic` clean, and the emitted HLSL compiles with DXC to DXIL (`ps_6_2`) and
  SPIR-V.
- Not measurable in the headless sandbox: GPU frame time and the visual result. Compare
  `r_rayTracedShadowsSamples 1` vs `4` vs `8`, and `r_rayTracedShadowsAdaptive` on/off, with the
  Editor profiler on real hardware.

## Known limits / next increments

- Only the first directional light (point / spot / disk lights: see increment 4).
- No spatial/temporal denoiser yet: at low sample counts the penumbra shows the noise pattern
  (smoothed by TAA when `r_rayTracedShadowsAnimateNoise` is on).
- The adaptive early-out can miss an occluder thinner than the probe spacing inside the cone; set
  `r_rayTracedShadowsAdaptive false` if thin geometry (wires, fences) loses its penumbra.

# Increment 4: soft shadows from point, spot and disk lights

The fullscreen pass now also shades up to `r_rayTracedShadowsLocalLights` (default 4, max 16)
local lights per frame, besides the first directional light. Lights covered: Point (sphere), Disk
(the Spot light component's default) and the Simple point / Simple spot lights.

## How it works

- **Light selection** (CPU, every frame): every light of the four feature processors is collected,
  lights whose attenuation radius does not come within `r_rayTracedShadowsMaxDistance` of the camera
  or with zero intensity are dropped, and the rest are ranked by `intensity / max(1, distance²)` to
  the camera (`AZ::SelectShadowLights`). The top N are uploaded to a fixed-size structured buffer
  (re-uploaded only when a light changed).
- **Per pixel, per light**: the light's illuminance at the pixel is computed exactly like the Atom
  PBR light shaders (inverse square, windowed to 0 at the attenuation radius, spot cone smoothstep;
  a disk light without a cone emits over its front hemisphere). Lights that do not reach the pixel
  cost nothing. Otherwise shadow rays go from the pixel toward **points on the light**: a Vogel disk
  of the bulb radius facing the pixel for a sphere light, or the light's own disk for a disk light.
  Rays end at the light, so geometry behind it never occludes, and they stop
  `r_rayTracedShadowsLightClearance` (0.1 m) short so the light's own fixture does not shadow it.
- **Softness**: the penumbra comes from the light's existing *Bulb radius* / *Disk radius*, with the
  same `r_rayTracedShadowsSamples`, adaptive probes and noise rotation as the sun. Radius 0 (and the
  Simple lights, which have none) gives hard shadows with one ray.
- **Composite**: the pass still writes one darkening factor per pixel, multiplied onto the lit
  image. It is now built from the fraction of the pixel's incoming light that is blocked: each
  light's visibility is weighted by the illuminance it delivers there (the sun by its lux), so a
  lamp's shadow is faint outdoors in daylight and full strength indoors
  (`AZ::CombineShadowVisibility`). With only a directional light the output is unchanged; with no
  light at all the default sun is used as before. A light that is in the scene but has intensity 0
  no longer casts a shadow.

| Layer | File | Role |
| --- | --- | --- |
| CPU core (reference) | `Code/Framework/AzCore/AzCore/Math/RayTracedShadows.{h,cpp}` | `LocalShadowLight`, `ComputeLocalLightIlluminance`, `ComputeLocalLightSamplePoint`, `ComputeLocalLightShadowVisibility`, `CombineShadowVisibility`, `SelectShadowLights`. |
| CPU tests | `Code/Framework/AzCore/Tests/Math/RayTracedShadowsTests.cpp` | 9 new tests (below). |
| CPU benchmark | `Code/Framework/AzCore/Tests/Math/RayTracingPerformanceTests.cpp` | `BM_RayTracingBvh/LocalLightShadowBatch/<tris>/<adaptive>`. |
| GPU shader | `Gems/Atom/Feature/Common/Assets/Shaders/RayTracing/RayTracedShadowsFullscreen.azsl` | `LocalShadowLight` buffer + per-light loop mirroring the CPU core. |
| Pass | `.../RayTracing/RayTracedShadowsFullscreenPass.{h,cpp}` | `SetLocalLights`, 64-byte packed light buffer (16 slots), new SRG constants. |
| Feature processor | `.../RayTracing/RayTracedShadowsFeatureProcessor.{h,cpp}` | Gathers lights, new cvars. The four light feature processors get a read-only `GetLightData()`; the directional one `GetFirstLightRgbIntensity()`. |

## Verification

- New `MATH_RayTracedShadows` tests: illuminance follows the inverse square and is 0 beyond the
  attenuation radius; spot illuminance is full inside the inner cone, partial between the cones and 0
  outside / behind the light; an occluder between pixel and light shadows it, one behind the light
  does not; the clearance ignores a lamp-shade quad 5 cm from the light; sample points lie on the
  light's disk (facing the receiver for spheres, in the disk plane for disk lights) and collapse to
  the center for radius 0 / one sample; the penumbra is monotonic and its width matches
  `2 · radius · occluder-to-receiver / light-to-occluder`; adaptive equals full sampling except on at
  most 2% of samples; the weighted combine; light selection by score and range.
- CPU benchmark (`LocalLightShadowBatch`, 0.5 m bulb, 8 rays/sample, Linux VM): adaptive 2.0x faster
  on 16K triangles (414k vs 208k samples/s) and 1.8x on 131K (204k vs 115k).
- Shader: `azslc --semantic` clean, DXC compiles the emitted HLSL to DXIL (`ps_6_2`) and SPIR-V.
- Not measurable in the headless sandbox: the visual result and GPU cost. Each local light can cost
  as much as the sun for the pixels it reaches; compare `r_rayTracedShadowsLocalLights 0` vs `4` with
  the Editor profiler.

## Known limits

- A light mesh that encloses the light beyond the clearance (a closed lamp shade) shadows everything:
  raise `r_rayTracedShadowsLightClearance` or exclude the mesh.
- The cone of a disk light with *Use cone angle* is measured from the disk center, not from the cone
  tip behind it, so the cone edge is slightly wider than the lit area.
- One combined factor: a pixel lit by a red and a blue light, with only the red one blocked, darkens
  both colors by the same blocked fraction.
- Projected shadow maps of those lights (if enabled) still apply on top.
