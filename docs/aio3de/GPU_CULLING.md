# GPU-driven frustum culling (compute + indirect draw)

This is the GPU counterpart of the SoA+SIMD CPU culling kernel
(`AZ::FrustumClassifySpheres`, see PR #4). It moves per-instance broad-phase
visibility off the CPU: a compute shader classifies every instance's bounding
sphere against the view frustum and compacts the survivors into a
`DrawIndexedIndirect` arguments buffer, so a single indirect draw renders exactly
the visible instances.

This is an **opt-in building block**. It is registered with the pass system but
is **not** part of any default render pipeline, so existing pipelines render
identically until you wire it in.

## Pieces

| File | Role |
| --- | --- |
| `Gems/Atom/Feature/Common/Assets/Shaders/Culling/FrustumCull.azsli` | Shared cull math; GPU mirror of `AZ::FrustumClassifySpheres` (plane convention, 3-state classify, exterior-wins). |
| `Gems/Atom/Feature/Common/Assets/Shaders/Culling/GpuFrustumCull.azsl` | Compute kernel: one thread per instance → classify → `InterlockedAdd` on the indirect instance count → write compacted index. |
| `Gems/Atom/Feature/Common/Assets/Shaders/Culling/GpuFrustumCull.shader` | Shader asset (entry `MainCS`, Compute). |
| `Gems/Atom/Feature/Common/Assets/Passes/GpuFrustumCull.pass` | `GpuFrustumCullTemplate` pass template, `PassClass` `GpuFrustumCullPass`. |
| `Gems/Atom/Feature/Common/Code/Source/Culling/GpuFrustumCullPass.{h,cpp}` | `RPI::ComputePass` subclass: owns the input/output buffers, derives frustum planes from the view, resets the indirect counter per frame, dispatches one thread per instance. |

## Data flow

```
per-instance spheres (float4 center.xyz + radius)   ── input  ─┐
view world-to-clip → 6 frustum planes (float4 n.xyz,d) ─ SRG ──┤
                                                               ▼
                                  GpuFrustumCull.azsl  (compute)
                                                               │  classify
                                                               │  if visible: slot = InterlockedAdd(args[1], 1)
                                                               ▼
visibleInstanceIndices[slot] = instanceIndex   ── output ──────┤
drawIndirectArgs[1] (instanceCount)            ── output ──────┘
                                                               ▼
                                  DrawIndexedIndirect(drawIndirectArgs)
```

`drawIndirectArgs` is the 5-uint `DrawIndexedIndirect` layout
(`indexCountPerInstance, instanceCount, startIndex, baseVertex, startInstance`),
matching `RHI::DrawIndexedIndirect` / `VkDrawIndexedIndirectCommand`. The pass
resets `instanceCount` to 0 each frame; the shader accumulates it.

## CPU/GPU correctness

`FrustumCull.azsli` mirrors `AzCore/Math/FrustumCull.inl` line-for-line:
per plane `distance = dot(n, center) + d`; a sphere is **exterior** (culled) if
`distance < -radius` for any plane, **overlaps** if `abs(distance) < radius`, else
**interior**; exterior wins (same early-out as `Frustum::IntersectSphere`). The
shader is compile-verified with `azslc` (`--semantic` clean, `numthreads(64,1,1)`).

## Wiring it into a pipeline

1. Add a `GpuFrustumCullTemplate` pass to your render pipeline `.pass` before the
   geometry pass, connecting `DrawIndirectArgs` / `VisibleInstanceIndices` to the
   consuming draw pass.
2. Feed per-instance data: `GpuFrustumCullPass::SetInstanceSpheres(spheres, indexCountPerInstance)`.
3. Have the geometry pass issue a `DrawIndexedIndirect` using
   `GpuFrustumCullPass::GetIndirectArgsBuffer()` and index instances via
   `GetVisibleInstanceIndicesBuffer()`.

## Profiling on real hardware

Frame-time **cannot** be measured meaningfully in CI/headless (software
rasterizer). On a real GPU:

- Build `profile` and run a scene with many instances (thousands+).
- Capture with **RenderDoc** or the platform GPU profiler; compare the geometry
  pass with CPU culling vs. this GPU cull + indirect draw.
- Watch: compute dispatch time, draw-call count, and CPU time spent walking the
  per-instance visibility list (should drop to ~0 with GPU culling).
- The win scales with instance count and CPU draw-submission overhead, mirroring
  the CPU kernel's 10× speedup at large working sets (PR #4).
