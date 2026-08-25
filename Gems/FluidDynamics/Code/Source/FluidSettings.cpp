/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "FluidSettings.h"

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace FluidDynamics
{
    void FluidSettings::ApplyPreset()
    {
        switch (m_preset)
        {
        case FluidPreset::Water:
            m_restDensity = 1000.0f;
            m_viscosity = 0.02f;
            break;
        case FluidPreset::Honey:
            m_restDensity = 1400.0f;
            m_viscosity = 1.0f;
            break;
        case FluidPreset::Custom:
            break;
        }
    }

    void FluidSettings::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<FluidSettings>()
                ->Version(2)
                ->Field("Preset", &FluidSettings::m_preset)
                ->Field("RestDensity", &FluidSettings::m_restDensity)
                ->Field("Viscosity", &FluidSettings::m_viscosity)
                ->Field("ParticleSpacing", &FluidSettings::m_particleSpacing)
                ->Field("Substeps", &FluidSettings::m_substeps)
                ->Field("Iterations", &FluidSettings::m_iterations)
                ->Field("GravityScale", &FluidSettings::m_gravityScale)
                ->Field("Damping", &FluidSettings::m_damping)
                ->Field("SpawnHalfExtents", &FluidSettings::m_spawnHalfExtents)
                ->Field("ContainerEnabled", &FluidSettings::m_containerEnabled)
                ->Field("ContainerHalfExtents", &FluidSettings::m_containerHalfExtents)
                ->Field("ContainerRestitution", &FluidSettings::m_containerRestitution)
                ->Field("AffectedByWind", &FluidSettings::m_affectedByWind)
                ->Field("WindDrag", &FluidSettings::m_windDrag)
                ->Field("Visualization", &FluidSettings::m_visualization)
                ->Field("Parallel", &FluidSettings::m_parallel);

            if (auto* editContext = serializeContext->GetEditContext())
            {
                editContext->Class<FluidSettings>("Fluid Settings", "Position based fluid simulation settings")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->DataElement(AZ::Edit::UIHandlers::ComboBox, &FluidSettings::m_preset,
                        "Preset", "Physical parameter preset; Custom keeps the values below untouched")
                        ->EnumAttribute(FluidPreset::Water, "Water")
                        ->EnumAttribute(FluidPreset::Honey, "Honey")
                        ->EnumAttribute(FluidPreset::Custom, "Custom")
                        ->Attribute(AZ::Edit::Attributes::ChangeNotify, AZ::Edit::PropertyRefreshLevels::ValuesOnly)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FluidSettings::m_restDensity,
                        "Rest density", "Target fluid density in kg/m^3 (water 1000, honey ~1400)")
                        ->Attribute(AZ::Edit::Attributes::Min, 1.0f)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FluidSettings::m_viscosity,
                        "Viscosity", "0.01-0.05 = water, 0.5-1 = honey/syrup")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 1.0f)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FluidSettings::m_particleSpacing,
                        "Particle spacing", "Rest distance between particles in meters (smaller = finer detail, more particles)")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.02f)
                        ->Attribute(AZ::Edit::Attributes::Max, 1.0f)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FluidSettings::m_substeps,
                        "Substeps", "Simulation substeps per frame")
                        ->Attribute(AZ::Edit::Attributes::Min, 1u)
                        ->Attribute(AZ::Edit::Attributes::Max, 8u)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FluidSettings::m_iterations,
                        "Iterations", "Density solver iterations per substep")
                        ->Attribute(AZ::Edit::Attributes::Min, 1u)
                        ->Attribute(AZ::Edit::Attributes::Max, 16u)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FluidSettings::m_gravityScale,
                        "Gravity scale", "Multiplier on world gravity")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FluidSettings::m_damping,
                        "Damping", "Extra per-second velocity damping")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 1.0f)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FluidSettings::m_spawnHalfExtents,
                        "Spawn half extents", "Local-space half extents of the box filled with fluid at play start")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FluidSettings::m_containerEnabled,
                        "Container enabled", "Keep particles inside the container box; when off the fluid spills freely")
                        ->Attribute(AZ::Edit::Attributes::ChangeNotify, AZ::Edit::PropertyRefreshLevels::EntireTree)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FluidSettings::m_containerHalfExtents,
                        "Container half extents", "Local-space half extents of the box that contains the fluid")
                        ->Attribute(AZ::Edit::Attributes::Visibility, &FluidSettings::IsContainerEnabled)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FluidSettings::m_containerRestitution,
                        "Container restitution", "Bounce on container walls: 0 = none, 1 = full")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 1.0f)
                        ->Attribute(AZ::Edit::Attributes::Visibility, &FluidSettings::IsContainerEnabled)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FluidSettings::m_affectedByWind,
                        "Affected by wind", "Sample Wind components in the level and apply their drag to the fluid")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FluidSettings::m_windDrag,
                        "Wind drag", "Drag coefficient coupling the fluid to the wind")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 10.0f)
                    ->DataElement(AZ::Edit::UIHandlers::ComboBox, &FluidSettings::m_visualization,
                        "Visualization", "How to draw the fluid particles (debug/basis rendering)")
                        ->EnumAttribute(FluidVisualization::None, "None")
                        ->EnumAttribute(FluidVisualization::Points, "Points")
                        ->EnumAttribute(FluidVisualization::Spheres, "Spheres")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FluidSettings::m_parallel,
                        "Parallel solve", "Distribute the solver across CPU worker threads");
            }
        }
    }
} // namespace FluidDynamics
