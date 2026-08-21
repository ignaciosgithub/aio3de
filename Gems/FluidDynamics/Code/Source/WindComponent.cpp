/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "WindComponent.h"

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace FluidDynamics
{
    void WindSettings::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<WindSettings>()
                ->Version(1)
                ->Field("Direction", &WindSettings::m_direction)
                ->Field("Speed", &WindSettings::m_speed)
                ->Field("GustStrength", &WindSettings::m_gustStrength)
                ->Field("GustFrequency", &WindSettings::m_gustFrequency)
                ->Field("Turbulence", &WindSettings::m_turbulence)
                ->Field("TurbulenceScale", &WindSettings::m_turbulenceScale);

            if (auto* editContext = serializeContext->GetEditContext())
            {
                editContext->Class<WindSettings>("Wind Settings", "Procedural wind field settings")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &WindSettings::m_direction,
                        "Direction", "World-space wind direction (normalized on use)")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &WindSettings::m_speed,
                        "Speed", "Steady wind speed in m/s")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 150.0f)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &WindSettings::m_gustStrength,
                        "Gust strength", "Gust amplitude as a fraction of the base speed")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 2.0f)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &WindSettings::m_gustFrequency,
                        "Gust frequency", "Gusts per second")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 10.0f)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &WindSettings::m_turbulence,
                        "Turbulence", "Spatial turbulence amplitude as a fraction of the base speed")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 2.0f)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &WindSettings::m_turbulenceScale,
                        "Turbulence scale", "Size of turbulence eddies in meters")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.1f)
                        ->Attribute(AZ::Edit::Attributes::Max, 100.0f);
            }
        }
    }

    void WindComponent::Reflect(AZ::ReflectContext* context)
    {
        WindSettings::Reflect(context);

        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<WindComponent, AZ::Component>()
                ->Version(1)
                ->Field("Settings", &WindComponent::m_settings);
        }
    }

    void WindComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("WindService"));
    }

    WindComponent::WindComponent(const WindSettings& settings)
        : m_settings(settings)
    {
    }

    void WindComponent::Activate()
    {
        AZ::WindField::Config config;
        const AZ::Vector3 direction =
            m_settings.m_direction.IsZero() ? AZ::Vector3(1.0f, 0.0f, 0.0f) : m_settings.m_direction.GetNormalized();
        config.m_baseVelocity = direction * m_settings.m_speed;
        config.m_gustStrength = m_settings.m_gustStrength;
        config.m_gustFrequency = m_settings.m_gustFrequency;
        config.m_turbulence = m_settings.m_turbulence;
        config.m_turbulenceScale = m_settings.m_turbulenceScale;
        m_windField.SetConfig(config);
        m_time = 0.0f;

        WindRequestBus::Handler::BusConnect();
        AZ::TickBus::Handler::BusConnect();
    }

    void WindComponent::Deactivate()
    {
        AZ::TickBus::Handler::BusDisconnect();
        WindRequestBus::Handler::BusDisconnect();
    }

    void WindComponent::AccumulateWind(const AZ::Vector3& position, AZ::Vector3& accumulated) const
    {
        accumulated += m_windField.Sample(position, m_time);
    }

    void WindComponent::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        m_time += deltaTime;
    }
} // namespace FluidDynamics
