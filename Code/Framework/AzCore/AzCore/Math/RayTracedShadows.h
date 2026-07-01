/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Math/RayTracingBvh.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/std/containers/vector.h>

namespace AZ
{
    //! A shaded surface point against which a hard shadow ray is cast.
    struct ShadowSample
    {
        Vector3 m_position; //!< World-space surface position (e.g. reconstructed from the depth buffer).
        Vector3 m_normal;   //!< World-space surface normal, used to offset the ray origin off the surface.
    };

    //! Parameters shared by every shadow ray in a batch.
    //!
    //! The light direction here points *from the surface toward the light* (i.e. the direction the
    //! occlusion ray travels). For a directional/sun light it is constant for every sample; for a
    //! point/spot light a caller computes it per sample (see ComputeShadowVisibility).
    struct ShadowRayParams
    {
        //! Direction from surface toward the light; need not be normalized (it is normalized internally).
        Vector3 m_toLight = Vector3(0.0f, 0.0f, 1.0f);

        //! Maximum ray distance. For a directional light use a large value spanning the scene; for a
        //! point/spot light use the distance to the light so geometry behind the light does not occlude.
        float m_maxDistance = 1.0e30f;

        //! Surface offset (along the normal) applied to the ray origin to avoid self-shadow acne.
        float m_normalBias = 1.0e-3f;
    };

    //! Hardware-agnostic hard shadow: returns surface visibility toward a light by casting a single
    //! occlusion ray against a portable BVH (no DXR / VK_KHR_ray_tracing / RT cores required).
    //!
    //! Returns 1.0 if the light is unoccluded for this sample, 0.0 if an occluder is hit within
    //! params.m_maxDistance. This is the CPU reference implementation; the GPU compute shader
    //! (RayTracedShadows.azsl) mirrors it sample-for-sample. Uses RayTracingBvh::IntersectAny so it
    //! stops at the first occluder.
    //!
    //! @param bvh    BVH built over the occluder triangles.
    //! @param sample surface position + normal to shade.
    //! @param params light direction, max distance and normal bias.
    AZCORE_API float ComputeShadowVisibility(
        const RayTracingBvh& bvh, const ShadowSample& sample, const ShadowRayParams& params);

    //! Batch form of ComputeShadowVisibility for a directional light (constant m_toLight): one
    //! visibility value per sample, in the same order as \p samples. This is the data-parallel hot
    //! path the GPU compute pass dispatches one thread per sample for.
    AZCORE_API void ComputeDirectionalShadowVisibility(
        const RayTracingBvh& bvh,
        const AZStd::vector<ShadowSample>& samples,
        const ShadowRayParams& params,
        AZStd::vector<float>& outVisibility);
} // namespace AZ
