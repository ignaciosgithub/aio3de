/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include "FluidSettings.h"

#include <AzCore/Component/Component.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Math/Fluid.h>

namespace FluidDynamics
{
    //! Simulates a particle fluid (position based fluids) inside an axis-aligned container box
    //! centered on the entity. At activation the spawn box is filled with particles at the
    //! configured spacing; the fluid then flows under gravity, wind drag, and its own pressure
    //! and viscosity. Water and honey are the built-in presets; any liquid in between is a
    //! matter of rest density and viscosity.
    class FluidVolumeComponent
        : public AZ::Component
        , private AZ::TickBus::Handler
    {
    public:
        AZ_COMPONENT(FluidVolumeComponent, "{6C2E8F41-9A0B-4D57-B3E6-1F2A3C4D5E60}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required);

        FluidVolumeComponent() = default;
        explicit FluidVolumeComponent(const FluidSettings& settings);

        const AZ::FluidSim& GetSimulation() const { return m_fluid; }

    protected:
        void Activate() override;
        void Deactivate() override;

        // TickBus
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;
        int GetTickOrder() override;

    private:
        void SpawnParticles();
        void DrawParticles() const;

        FluidSettings m_settings;
        AZ::FluidSim m_fluid;
        float m_time = 0.0f;
        mutable AZStd::vector<AZ::Vector3> m_drawPositions;
    };
} // namespace FluidDynamics
