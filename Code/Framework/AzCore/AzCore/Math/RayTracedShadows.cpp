/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Math/RayTracedShadows.h>

namespace AZ
{
    float ComputeShadowVisibility(
        const RayTracingBvh& bvh, const ShadowSample& sample, const ShadowRayParams& params)
    {
        // Offset the origin off the surface along the normal to avoid the surface shadowing itself
        // (shadow acne) at the ray origin.
        const Vector3 origin = sample.m_position + sample.m_normal * params.m_normalBias;
        const Vector3 direction = params.m_toLight.GetNormalizedSafe();

        // Occluded => in shadow (0), otherwise fully lit (1). IntersectAny early-outs on first hit.
        return bvh.IntersectAny(origin, direction, params.m_maxDistance) ? 0.0f : 1.0f;
    }

    void ComputeDirectionalShadowVisibility(
        const RayTracingBvh& bvh,
        const AZStd::vector<ShadowSample>& samples,
        const ShadowRayParams& params,
        AZStd::vector<float>& outVisibility)
    {
        outVisibility.resize(samples.size());
        for (size_t i = 0; i < samples.size(); ++i)
        {
            outVisibility[i] = ComputeShadowVisibility(bvh, samples[i], params);
        }
    }
} // namespace AZ
