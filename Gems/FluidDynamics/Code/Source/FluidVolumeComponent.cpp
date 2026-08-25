/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "FluidVolumeComponent.h"
#include "WindBus.h"

#include <AzCore/Component/TransformBus.h>
#include <AzCore/Math/Color.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <Atom/RPI.Public/AuxGeom/AuxGeomDraw.h>
#include <Atom/RPI.Public/AuxGeom/AuxGeomFeatureProcessorInterface.h>
#include <Atom/RPI.Public/Scene.h>

namespace FluidDynamics
{
    void FluidVolumeComponent::Reflect(AZ::ReflectContext* context)
    {
        FluidSettings::Reflect(context);

        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<FluidVolumeComponent, AZ::Component>()
                ->Version(1)
                ->Field("Settings", &FluidVolumeComponent::m_settings);
        }
    }

    void FluidVolumeComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("FluidVolumeService"));
    }

    void FluidVolumeComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("FluidVolumeService"));
    }

    void FluidVolumeComponent::GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required)
    {
        required.push_back(AZ_CRC_CE("TransformService"));
    }

    FluidVolumeComponent::FluidVolumeComponent(const FluidSettings& settings)
        : m_settings(settings)
    {
    }

    void FluidVolumeComponent::Activate()
    {
        m_settings.ApplyPreset();
        m_time = 0.0f;
        SpawnParticles();
        AZ::TickBus::Handler::BusConnect();
    }

    void FluidVolumeComponent::Deactivate()
    {
        AZ::TickBus::Handler::BusDisconnect();
        m_fluid.Clear();
        m_drawPositions = {};
    }

    void FluidVolumeComponent::SpawnParticles()
    {
        AZ::Vector3 center = AZ::Vector3::CreateZero();
        AZ::TransformBus::EventResult(center, GetEntityId(), &AZ::TransformBus::Events::GetWorldTranslation);

        AZ::FluidConfig config;
        config.m_gravity = AZ::Vector3(0.0f, 0.0f, -9.81f * m_settings.m_gravityScale);
        config.m_restDensity = m_settings.m_restDensity;
        config.m_viscosity = m_settings.m_viscosity;
        config.m_particleSpacing = m_settings.m_particleSpacing;
        config.m_substeps = m_settings.m_substeps;
        config.m_iterations = m_settings.m_iterations;
        config.m_damping = m_settings.m_damping;
        config.m_boundsEnabled = m_settings.m_containerEnabled;
        config.m_bounds = AZ::Aabb::CreateCenterHalfExtents(center, m_settings.m_containerHalfExtents);
        config.m_boundsRestitution = m_settings.m_containerRestitution;
        config.m_parallel = m_settings.m_parallel;

        m_fluid.Clear();
        m_fluid.SetConfig(config);
        m_fluid.SpawnBox(center - m_settings.m_spawnHalfExtents, center + m_settings.m_spawnHalfExtents);

        if (m_settings.m_affectedByWind)
        {
            const float drag = m_settings.m_windDrag;
            m_fluid.SetExternalAcceleration(
                [drag](const AZ::Vector3& position, const AZ::Vector3& velocity)
                {
                    return (SampleTotalWind(position) - velocity) * drag;
                });
        }
        else
        {
            m_fluid.SetExternalAcceleration({});
        }
    }

    void FluidVolumeComponent::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        m_time += deltaTime;
        m_fluid.Step(AZStd::min(deltaTime, 1.0f / 20.0f));
        DrawParticles();
    }

    int FluidVolumeComponent::GetTickOrder()
    {
        return AZ::TICK_PHYSICS;
    }

    void FluidVolumeComponent::DrawParticles() const
    {
        if (m_settings.m_visualization == FluidVisualization::None)
        {
            return;
        }

        AZ::RPI::Scene* scene = AZ::RPI::Scene::GetSceneForEntityId(GetEntityId());
        if (!scene)
        {
            return;
        }
        AZ::RPI::AuxGeomDrawPtr auxGeom = AZ::RPI::AuxGeomFeatureProcessorInterface::GetDrawQueueForScene(scene);
        if (!auxGeom)
        {
            return;
        }

        const AZStd::vector<AZ::FluidParticle>& particles = m_fluid.GetParticles();
        const AZ::Color color(0.25f, 0.55f, 0.95f, 1.0f);

        if (m_settings.m_visualization == FluidVisualization::Points)
        {
            m_drawPositions.resize(particles.size());
            for (size_t i = 0; i < particles.size(); ++i)
            {
                m_drawPositions[i] = particles[i].m_position;
            }
            AZ::RPI::AuxGeomDraw::AuxGeomDynamicDrawArguments args;
            args.m_verts = m_drawPositions.data();
            args.m_vertCount = static_cast<uint32_t>(m_drawPositions.size());
            args.m_colors = &color;
            args.m_colorCount = 1;
            args.m_size = 4;
            auxGeom->DrawPoints(args);
        }
        else
        {
            const float radius = 0.5f * m_settings.m_particleSpacing;
            for (const AZ::FluidParticle& particle : particles)
            {
                auxGeom->DrawSphere(particle.m_position, radius, color, AZ::RPI::AuxGeomDraw::DrawStyle::Shaded);
            }
        }
    }
} // namespace FluidDynamics
