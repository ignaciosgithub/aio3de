/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AtomLyIntegration/CommonFeatures/Mesh/MeshComponentBus.h>
#include <AzCore/Component/Component.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Math/Transform.h>
#include <AzCore/std/containers/vector.h>

#include "SoftBodySettings.h"

#include <AzCore/Math/SoftBody.h>

namespace SoftBodyPhysics
{
    //! Runtime soft body component: converts the entity's render mesh (LOD 0) into an XPBD
    //! particle system on model load, simulates it every tick and writes the deformed
    //! positions/normals back into this entity's (cloned) model instance.
    class SoftBodyComponent
        : public AZ::Component
        , private AZ::Render::MeshComponentNotificationBus::Handler
        , private AZ::Render::UniqueModelInstanceRequestBus::Handler
        , private AZ::TickBus::Handler
    {
    public:
        AZ_COMPONENT(SoftBodyComponent, "{8F1A2B3C-4D5E-6F70-8192-A3B4C5D6E7F8}");

        SoftBodyComponent() = default;
        explicit SoftBodyComponent(const SoftBodySettings& settings);

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required);

    protected:
        // AZ::Component
        void Activate() override;
        void Deactivate() override;

        // AZ::Render::UniqueModelInstanceRequestBus
        bool RequiresUniqueModelInstance() const override { return true; }

        // AZ::Render::MeshComponentNotificationBus
        void OnModelReady(const AZ::Data::Asset<AZ::RPI::ModelAsset>& modelAsset, const AZ::Data::Instance<AZ::RPI::Model>& model) override;
        void OnModelPreDestroy() override;

        // AZ::TickBus
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;
        int GetTickOrder() override;

    private:
        struct SubMeshRange
        {
            uint32_t m_subMeshIndex = 0;
            uint32_t m_vertexCount = 0;
            uint32_t m_firstParticle = 0; //!< Offset into the per-vertex arrays below.
        };

        void BuildSimulation(const AZ::Data::Instance<AZ::RPI::Model>& model);
        void WriteRenderData(bool restoreOriginal);
        void ReleaseSimulation();

        SoftBodySettings m_settings;

        AZ::Data::Instance<AZ::RPI::Model> m_model;
        AZStd::vector<SubMeshRange> m_subMeshes;

        //! Per original render vertex (concatenated across submeshes).
        AZStd::vector<AZ::Vector3> m_originalPositions; //!< Local space, for restore on deactivate.
        AZStd::vector<AZ::Vector3> m_originalNormals;
        AZStd::vector<int32_t> m_weldedIndex;           //!< Render vertex -> simulated particle.

        //! Triangle list in welded-particle space (for normal recomputation).
        AZStd::vector<uint32_t> m_weldedTriangles;

        AZ::SoftBody m_softBody;
        AZ::Transform m_worldTM = AZ::Transform::CreateIdentity();
        bool m_simulationReady = false;
        bool m_requestedModelRefresh = false;
    };
} // namespace SoftBodyPhysics
