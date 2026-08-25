/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Math/Vector3.h>
#include <AzCore/RTTI/ReflectContext.h>
#include <AzCore/RTTI/TypeInfoSimple.h>

namespace FluidDynamics
{
    enum class FluidPreset : AZ::u8
    {
        Water = 0,
        Honey = 1,
        Custom = 2
    };

    enum class FluidVisualization : AZ::u8
    {
        None = 0,
        Points = 1,
        Spheres = 2
    };

    //! User-facing settings of a Fluid Volume component.
    struct FluidSettings
    {
        AZ_TYPE_INFO(FluidSettings, "{0D9E4A21-6B3F-4C8E-9A57-2E1F0B6D7C34}");

        static void Reflect(AZ::ReflectContext* context);

        //! Applies the preset's physical parameters (rest density and viscosity) unless Custom.
        void ApplyPreset();

        FluidPreset m_preset = FluidPreset::Water;
        float m_restDensity = 1000.0f;
        float m_viscosity = 0.02f;
        float m_particleSpacing = 0.15f;
        AZ::u32 m_substeps = 2;
        AZ::u32 m_iterations = 3;
        float m_gravityScale = 1.0f;
        float m_damping = 0.0f;

        //! Fluid spawn region as a local-space box half-extent around the entity origin.
        AZ::Vector3 m_spawnHalfExtents = AZ::Vector3(0.5f, 0.5f, 0.5f);
        //! Keep particles inside the container box; when off the fluid spills freely.
        bool m_containerEnabled = true;
        //! Container box (local-space half extents); particles are kept inside it.
        AZ::Vector3 m_containerHalfExtents = AZ::Vector3(1.0f, 1.0f, 1.0f);
        float m_containerRestitution = 0.0f;

        bool IsContainerEnabled() const { return m_containerEnabled; }

        bool m_affectedByWind = false;
        float m_windDrag = 0.5f;

        FluidVisualization m_visualization = FluidVisualization::Spheres;
        bool m_parallel = true;
    };
} // namespace FluidDynamics
