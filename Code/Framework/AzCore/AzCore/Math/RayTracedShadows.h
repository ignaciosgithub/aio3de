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
#include <AzCore/base.h>

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

    //! Soft shadows from a directional light that has a visible disk (e.g. the sun): several shadow
    //! rays per sample are spread over the light's cone, and the visibility is the fraction that
    //! reach the light. This gives a penumbra whose width grows with the occluder-to-receiver
    //! distance (contact hardening), unlike a filtered shadow map.
    struct SoftShadowParams
    {
        //! Angular radius of the light's disk, in radians. 0 = infinitely small light (hard shadow).
        float m_angularRadius = 0.0f;

        //! Rays per sample spread over the light's disk. 1 = hard shadow (one ray toward the center).
        uint32_t m_sampleCount = 1;

        //! Cast GetSoftShadowProbeCount() probe rays first and stop if they all agree (fully lit or
        //! fully shadowed), so only penumbra samples pay for all m_sampleCount rays.
        bool m_adaptive = true;

        //! Rotation (radians) of the sample pattern around the light direction. The GPU varies it per
        //! pixel so the fixed pattern turns into fine noise instead of banding.
        float m_rotation = 0.0f;
    };

    //! Number of probe rays the adaptive soft-shadow path casts before deciding whether a sample is
    //! in the penumbra: max(2, sampleCount / 4), clamped to sampleCount.
    AZCORE_API uint32_t GetSoftShadowProbeCount(uint32_t sampleCount);

    //! Direction of soft-shadow ray \p sampleIndex of \p sampleCount: a Vogel (golden-angle) disk
    //! point of radius tanAngularRadius, rotated by \p rotation, around the normalized \p toLight.
    //! Every direction is normalized and lies within the light's cone.
    AZCORE_API Vector3 ComputeSoftShadowRayDirection(
        const Vector3& toLight, float tanAngularRadius, uint32_t sampleIndex, uint32_t sampleCount, float rotation);

    //! Soft-shadow visibility in [0, 1] (fraction of the light's disk that is unoccluded). Falls back
    //! to ComputeShadowVisibility when soft.m_sampleCount <= 1 or soft.m_angularRadius <= 0. With
    //! soft.m_adaptive the probe rays (the last probeCount samples, on the disk rim) are cast first;
    //! if they all agree the result is 0 or 1 without casting the rest. This is the CPU reference
    //! that RayTracedShadowsFullscreen.azsl mirrors.
    AZCORE_API float ComputeSoftShadowVisibility(
        const RayTracingBvh& bvh, const ShadowSample& sample, const ShadowRayParams& params, const SoftShadowParams& soft);
} // namespace AZ
