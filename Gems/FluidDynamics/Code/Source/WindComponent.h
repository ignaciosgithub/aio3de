/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include "WindBus.h"

#include <AzCore/Component/Component.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Math/Fluid.h>

namespace FluidDynamics
{
    //! User-facing settings of a Wind component.
    struct WindSettings
    {
        AZ_TYPE_INFO(WindSettings, "{3F8B2C15-7D4E-49A0-B6C1-8E2F5A9D0B47}");

        static void Reflect(AZ::ReflectContext* context);

        AZ::Vector3 m_direction = AZ::Vector3(1.0f, 0.0f, 0.0f);
        float m_speed = 5.0f;          //!< Steady wind speed in m/s.
        float m_gustStrength = 0.5f;   //!< Gust amplitude as a fraction of the base speed.
        float m_gustFrequency = 0.3f;  //!< Gusts per second.
        float m_turbulence = 0.25f;    //!< Spatial turbulence amplitude as a fraction of base speed.
        float m_turbulenceScale = 4.0f; //!< Size of turbulence eddies in meters.
    };

    //! Global analytic wind: emits a procedural wind velocity field (steady flow + gusts +
    //! turbulence) that fluids and gameplay can sample via WindRequestBus / SampleTotalWind.
    //! Air/wind is intentionally not particle-simulated: an analytic field is orders of magnitude
    //! cheaper and can be evaluated at any point in the level.
    class WindComponent
        : public AZ::Component
        , private WindRequestBus::Handler
        , private AZ::TickBus::Handler
    {
    public:
        AZ_COMPONENT(WindComponent, "{A1D75E30-2B8C-4F6A-9E01-C3B4D5E6F708}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);

        WindComponent() = default;
        explicit WindComponent(const WindSettings& settings);

    protected:
        void Activate() override;
        void Deactivate() override;

        // WindRequestBus
        void AccumulateWind(const AZ::Vector3& position, AZ::Vector3& accumulated) const override;

        // TickBus
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;

    private:
        WindSettings m_settings;
        AZ::WindField m_windField;
        float m_time = 0.0f;
    };
} // namespace FluidDynamics
