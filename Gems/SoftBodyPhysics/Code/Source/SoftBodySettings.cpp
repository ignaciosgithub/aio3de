/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "SoftBodySettings.h"

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace SoftBodyPhysics
{
    void SoftBodySettings::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<SoftBodySettings>()
                ->Version(1)
                ->Field("MassPerVertex", &SoftBodySettings::m_massPerVertex)
                ->Field("Compliance", &SoftBodySettings::m_compliance)
                ->Field("Pressure", &SoftBodySettings::m_pressure)
                ->Field("Damping", &SoftBodySettings::m_damping)
                ->Field("Substeps", &SoftBodySettings::m_substeps)
                ->Field("Iterations", &SoftBodySettings::m_iterations)
                ->Field("GravityScale", &SoftBodySettings::m_gravityScale)
                ->Field("GroundCollision", &SoftBodySettings::m_groundCollision)
                ->Field("GroundHeight", &SoftBodySettings::m_groundHeight)
                ->Field("GroundFriction", &SoftBodySettings::m_groundFriction)
                ->Field("PinHighestVertices", &SoftBodySettings::m_pinHighestVertices)
                ->Field("PinTolerance", &SoftBodySettings::m_pinTolerance);

            if (auto* editContext = serializeContext->GetEditContext())
            {
                editContext->Class<SoftBodySettings>("Soft Body Settings", "XPBD soft body simulation settings")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &SoftBodySettings::m_massPerVertex,
                        "Mass per vertex", "Mass of each simulated vertex in kg")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.001f)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &SoftBodySettings::m_compliance,
                        "Stretch compliance", "Edge softness: 0 = rigid, larger = stretchier")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &SoftBodySettings::m_pressure,
                        "Pressure", "Volume preservation: 0 = off, 1 = keep rest volume, > 1 inflates")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &SoftBodySettings::m_damping,
                        "Damping", "Per-second velocity damping")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 1.0f)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &SoftBodySettings::m_substeps,
                        "Substeps", "Simulation substeps per frame (more = stiffer, more expensive)")
                        ->Attribute(AZ::Edit::Attributes::Min, 1u)
                        ->Attribute(AZ::Edit::Attributes::Max, 16u)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &SoftBodySettings::m_iterations,
                        "Iterations", "Constraint iterations per substep")
                        ->Attribute(AZ::Edit::Attributes::Min, 1u)
                        ->Attribute(AZ::Edit::Attributes::Max, 32u)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &SoftBodySettings::m_gravityScale,
                        "Gravity scale", "Multiplier on world gravity")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &SoftBodySettings::m_groundCollision,
                        "Ground collision", "Collide against a world-space horizontal plane")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &SoftBodySettings::m_groundHeight,
                        "Ground height", "World Z of the ground plane")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &SoftBodySettings::m_groundFriction,
                        "Ground friction", "Tangential friction on ground contact")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 1.0f)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &SoftBodySettings::m_pinHighestVertices,
                        "Pin highest vertices", "Pin the topmost vertices in place (hanging bodies)")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &SoftBodySettings::m_pinTolerance,
                        "Pin tolerance", "Vertices within this distance of the top are pinned (meters)")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f);
            }
        }
    }
} // namespace SoftBodyPhysics
