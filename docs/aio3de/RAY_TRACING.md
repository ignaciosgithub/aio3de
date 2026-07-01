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
