/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "SoftBodyComponent.h"
#include "SoftBodyRegistry.h"

#include <Atom/RPI.Public/Buffer/Buffer.h>
#include <Atom/RPI.Public/Model/Model.h>
#include <Atom/RPI.Reflect/Buffer/BufferAssetView.h>
#include <Atom/RPI.Reflect/Model/ModelAsset.h>
#include <Atom/RPI.Reflect/Model/ModelLodAsset.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Math/PackedVector3.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Math/Aabb.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/limits.h>
#include <AzFramework/Physics/Common/PhysicsSceneQueries.h>
#include <AzFramework/Physics/Common/PhysicsSimulatedBody.h>
#include <AzFramework/Physics/Common/PhysicsTypes.h>
#include <AzFramework/Physics/PhysicsScene.h>
#include <AzFramework/Physics/SimulatedBodies/RigidBody.h>

namespace SoftBodyPhysics
{
    namespace
    {
        constexpr float MaxTickDelta = 1.0f / 20.0f; //!< Clamp huge frame spikes for stability.

        //! Maps an RPI buffer of a model-instance mesh for CPU writes (multi-device aware).
        template<typename T>
        class MappedBuffer
        {
        public:
            MappedBuffer(const AZ::RPI::BufferAssetView* bufferAssetView, size_t expectedElementCount)
            {
                if (!bufferAssetView)
                {
                    return;
                }
                const AZ::RHI::BufferViewDescriptor& descriptor = bufferAssetView->GetBufferViewDescriptor();
                if (descriptor.m_elementCount != expectedElementCount || descriptor.m_elementSize != sizeof(T))
                {
                    return;
                }
                m_rpiBuffer = AZ::RPI::Buffer::FindOrCreate(bufferAssetView->GetBufferAsset());
                if (!m_rpiBuffer)
                {
                    return;
                }
                const uint64_t byteCount = aznumeric_cast<uint64_t>(descriptor.m_elementCount) * sizeof(T);
                const uint64_t byteOffset = aznumeric_cast<uint64_t>(descriptor.m_elementOffset) * sizeof(T);
                auto data = m_rpiBuffer->Map(byteCount, byteOffset);
                for (auto [deviceIndex, buffer] : data)
                {
                    m_buffer[deviceIndex] = static_cast<T*>(buffer);
                }
            }

            ~MappedBuffer()
            {
                if (!m_buffer.empty())
                {
                    m_rpiBuffer->Unmap();
                }
            }

            const AZStd::unordered_map<int, T*>& GetBuffer() const { return m_buffer; }

        private:
            AZ::Data::Instance<AZ::RPI::Buffer> m_rpiBuffer;
            AZStd::unordered_map<int, T*> m_buffer;
        };

        //! Quantized position key used to weld coincident render vertices into one particle.
        uint64_t PositionKey(const AZ::Vector3& position)
        {
            constexpr float Scale = 1.0f / 1e-4f; // 0.1 mm weld tolerance
            const auto quantize = [](float v)
            {
                return static_cast<uint64_t>(static_cast<int64_t>(v * Scale) & 0x1FFFFF);
            };
            return (quantize(position.GetX()) << 42) | (quantize(position.GetY()) << 21) | quantize(position.GetZ());
        }
    } // namespace

    //! Projects particles out of the level's physics colliders using sphere sweeps of the particle
    //! radius from each particle's pre-substep position along its motion. The MTD hit flag reports
    //! particles that are already overlapping a collider at the start of the sweep — this catches a
    //! rigid body moving into a resting soft body (whose particles have no motion of their own) and
    //! depenetrates particles instead of tunneling through. When \p includeRigidBodies is set,
    //! dynamic rigid bodies collide too and receive a push-back impulse at the contact point
    //! (two-way coupling).
    void SoftBodyComponent::SolveWorldContacts(
        AZStd::vector<AZ::SoftBodyParticle>& particles, float dt, const WorldContactSettings& contactSettings)
    {
        auto* sceneInterface = AZ::Interface<AzPhysics::SceneInterface>::Get();
        if (!sceneInterface)
        {
            return;
        }
        const AzPhysics::SceneHandle sceneHandle = sceneInterface->GetSceneHandle(AzPhysics::DefaultPhysicsSceneName);
        if (sceneHandle == AzPhysics::InvalidSceneHandle)
        {
            return;
        }

        constexpr float MinMotion = 1e-8f;
        const AzPhysics::SceneQuery::QueryType queryType = contactSettings.m_includeRigidBodies
            ? AzPhysics::SceneQuery::QueryType::StaticAndDynamic
            : AzPhysics::SceneQuery::QueryType::Static;
        AzPhysics::SceneQuery::FilterCallback filterCallback;
        if (contactSettings.m_selfEntityId.IsValid())
        {
            filterCallback =
                [selfEntityId = contactSettings.m_selfEntityId](
                    const AzPhysics::SimulatedBody* body, [[maybe_unused]] const Physics::Shape* shape)
            {
                return (body && body->GetEntityId() == selfEntityId)
                    ? AzPhysics::SceneQuery::QueryHitType::None
                    : AzPhysics::SceneQuery::QueryHitType::Block;
            };
        }
        // Broadphase: a single overlap test of the body's swept bounds. When the soft body is not
        // near any collider this skips the per-particle sweeps entirely, and otherwise yields the
        // nearby collider bounds so only particles close to one of them pay for a scene sweep.
        // Every scene query takes the PhysX scene read lock, so the solve stays single-threaded —
        // hammering that lock from many workers costs more in contention than the parallelism wins.
        AZ::Aabb bodyBounds = AZ::Aabb::CreateNull();
        for (const AZ::SoftBodyParticle& particle : particles)
        {
            bodyBounds.AddPoint(particle.m_position);
            bodyBounds.AddPoint(particle.m_prevPosition);
        }
        bodyBounds.Expand(AZ::Vector3(contactSettings.m_particleRadius));

        AzPhysics::OverlapRequest overlapRequest = AzPhysics::OverlapRequestHelpers::CreateBoxOverlapRequest(
            bodyBounds.GetExtents(), AZ::Transform::CreateTranslation(bodyBounds.GetCenter()));
        overlapRequest.m_queryType = queryType;
        overlapRequest.m_maxResults = 32;
        if (contactSettings.m_selfEntityId.IsValid())
        {
            overlapRequest.m_filterCallback =
                [selfEntityId = contactSettings.m_selfEntityId](
                    const AzPhysics::SimulatedBody* body, [[maybe_unused]] const Physics::Shape* shape)
            {
                return !(body && body->GetEntityId() == selfEntityId);
            };
        }
        AzPhysics::SceneQueryHits broadphaseHits;
        if (!sceneInterface->QueryScene(sceneHandle, &overlapRequest, broadphaseHits) || broadphaseHits.m_hits.empty())
        {
            return;
        }
        AZStd::vector<AZ::Aabb> colliderBounds;
        colliderBounds.reserve(broadphaseHits.m_hits.size());
        bool sweepAllParticles = false;
        for (const AzPhysics::SceneQueryHit& broadphaseHit : broadphaseHits.m_hits)
        {
            const AzPhysics::SimulatedBody* body =
                sceneInterface->GetSimulatedBodyFromHandle(sceneHandle, broadphaseHit.m_bodyHandle);
            if (!body)
            {
                // Unknown collider bounds: be conservative and sweep everything.
                sweepAllParticles = true;
                break;
            }
            AZ::Aabb bounds = body->GetAabb();
            bounds.Expand(AZ::Vector3(contactSettings.m_particleRadius));
            colliderBounds.push_back(bounds);
        }
        // The overlap result caps out at m_maxResults; if it is full there may be more colliders.
        if (broadphaseHits.m_hits.size() >= overlapRequest.m_maxResults)
        {
            sweepAllParticles = true;
        }

        //! Push-back impulse recorded during the sweep loop, applied afterwards.
        struct RigidPush
        {
            AzPhysics::SimulatedBodyHandle m_bodyHandle = AzPhysics::InvalidSimulatedBodyHandle;
            AZ::Vector3 m_impulse = AZ::Vector3::CreateZero(); //!< Unclamped; capped against the body mass on apply.
            AZ::Vector3 m_contactPoint = AZ::Vector3::CreateZero();
        };
        AZStd::vector<RigidPush> rigidPushes;
        if (contactSettings.m_includeRigidBodies)
        {
            rigidPushes.resize(particles.size());
        }

        const float clampedFriction = AZStd::clamp(contactSettings.m_friction, 0.0f, 1.0f);
        const float invDt = dt > 0.0f ? 1.0f / dt : 0.0f;

        AzPhysics::SceneQueryHits hits;
        for (size_t particleIndex = 0; particleIndex < particles.size(); ++particleIndex)
        {
            AZ::SoftBodyParticle& particle = particles[particleIndex];
            if (particle.m_invMass <= 0.0f)
            {
                continue;
            }
            const AZ::Vector3 motion = particle.m_position - particle.m_prevPosition;
            const float motionLength = motion.GetLength();

            if (!sweepAllParticles)
            {
                // Only pay for a scene sweep when this particle's swept segment is inside the
                // (radius-inflated) bounds of one of the nearby colliders.
                AZ::Aabb particleBounds = AZ::Aabb::CreateFromPoint(particle.m_position);
                particleBounds.AddPoint(particle.m_prevPosition);
                bool nearCollider = false;
                for (const AZ::Aabb& bounds : colliderBounds)
                {
                    if (bounds.Overlaps(particleBounds))
                    {
                        nearCollider = true;
                        break;
                    }
                }
                if (!nearCollider)
                {
                    continue;
                }
            }

            // A sweep direction is required even for resting particles: the zero-length sweep with
            // the MTD flag still reports colliders that moved into the particle since last substep.
            const AZ::Vector3 direction =
                motionLength >= MinMotion ? (motion / motionLength) : AZ::Vector3::CreateAxisZ(-1.0f);

            AzPhysics::ShapeCastRequest request = AzPhysics::ShapeCastRequestHelpers::CreateSphereCastRequest(
                contactSettings.m_particleRadius,
                AZ::Transform::CreateTranslation(particle.m_prevPosition),
                direction,
                AZStd::max(motionLength, MinMotion),
                queryType,
                AzPhysics::CollisionGroup::All,
                filterCallback);
            request.m_maxResults = 1;

            hits.m_hits.clear();
            if (!sceneInterface->QueryScene(sceneHandle, &request, hits) || hits.m_hits.empty())
            {
                continue;
            }
            const AzPhysics::SceneQueryHit& hit = hits.m_hits.front();

            AZ::Vector3 normal = hit.m_normal;
            if (normal.IsZero())
            {
                normal = -direction;
            }

            AZ::Vector3 targetPosition;
            if (hit.m_distance <= 0.0f)
            {
                // Initial overlap: MTD reports the depenetration direction in m_normal and the
                // penetration depth as a negative distance. Push the particle out of the collider.
                targetPosition = particle.m_position + normal * (-hit.m_distance);
            }
            else
            {
                // Sweep contact: stop the particle center where the sphere first touched.
                targetPosition = particle.m_prevPosition + direction * hit.m_distance;
            }

            // Two-way coupling: record the momentum removed from the particle so it can be
            // transferred to dynamic rigid bodies as an impulse after the sweep loop.
            if (contactSettings.m_includeRigidBodies && hit.m_bodyHandle != AzPhysics::InvalidSimulatedBodyHandle)
            {
                const AZ::Vector3 removedMotion = particle.m_position - targetPosition;
                const float particleMass = 1.0f / particle.m_invMass;
                RigidPush& push = rigidPushes[particleIndex];
                push.m_bodyHandle = hit.m_bodyHandle;
                push.m_impulse = removedMotion * (particleMass * invDt * contactSettings.m_rigidPushScale);
                push.m_contactPoint = hit.m_position;
            }

            particle.m_position = targetPosition;

            // Friction: pull the tangential motion of this substep back toward the entry point.
            const AZ::Vector3 newMotion = particle.m_position - particle.m_prevPosition;
            const AZ::Vector3 tangential = newMotion - normal * newMotion.Dot(normal);
            particle.m_position -= tangential * clampedFriction;
        }

        // Apply the recorded push-back impulses to dynamic rigid bodies.
        for (const RigidPush& push : rigidPushes)
        {
            if (push.m_bodyHandle == AzPhysics::InvalidSimulatedBodyHandle)
            {
                continue;
            }
            auto* rigidBody = azrtti_cast<AzPhysics::RigidBody*>(
                sceneInterface->GetSimulatedBodyFromHandle(sceneHandle, push.m_bodyHandle));
            if (!rigidBody || rigidBody->IsKinematic())
            {
                continue;
            }
            AZ::Vector3 impulse = push.m_impulse;

            // Cap the velocity change imparted on the body: a particle that starts a substep
            // already overlapping (e.g. bodies placed in contact in the editor) would otherwise
            // produce a near-infinite impulse from the full penetration depth over a tiny dt.
            const float maxImpulse = rigidBody->GetMass() * contactSettings.m_rigidMaxPushVelocity;
            const float impulseLength = impulse.GetLength();
            if (impulseLength > maxImpulse && impulseLength > 0.0f)
            {
                impulse *= maxImpulse / impulseLength;
            }
            rigidBody->ApplyLinearImpulseAtWorldPoint(impulse, push.m_contactPoint);
        }
    }

    SoftBodyComponent::SoftBodyComponent(const SoftBodySettings& settings)
        : m_settings(settings)
    {
    }

    void SoftBodyComponent::Reflect(AZ::ReflectContext* context)
    {
        SoftBodySettings::Reflect(context);

        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<SoftBodyComponent, AZ::Component>()
                ->Version(1)
                ->Field("Settings", &SoftBodyComponent::m_settings);
        }
    }

    void SoftBodyComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("SoftBodyPhysicsService"));
    }

    void SoftBodyComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("SoftBodyPhysicsService"));
    }

    void SoftBodyComponent::GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required)
    {
        required.push_back(AZ_CRC_CE("MeshService"));
    }

    void SoftBodyComponent::Activate()
    {
        const AZ::EntityId entityId = GetEntityId();
        m_requestedModelRefresh = false;
        AZ::Render::UniqueModelInstanceRequestBus::Handler::BusConnect(entityId);
        // Connecting delivers OnModelReady immediately if the model is already loaded.
        AZ::Render::MeshComponentNotificationBus::Handler::BusConnect(entityId);
    }

    void SoftBodyComponent::Deactivate()
    {
        AZ::TickBus::Handler::BusDisconnect();
        AZ::Render::MeshComponentNotificationBus::Handler::BusDisconnect();
        AZ::Render::UniqueModelInstanceRequestBus::Handler::BusDisconnect();
        if (m_simulationReady)
        {
            WriteRenderData(/*restoreOriginal=*/true);
        }
        ReleaseSimulation();
    }

    void SoftBodyComponent::OnModelReady(
        [[maybe_unused]] const AZ::Data::Asset<AZ::RPI::ModelAsset>& modelAsset, const AZ::Data::Instance<AZ::RPI::Model>& model)
    {
        if (!model)
        {
            return;
        }

        // A unique per-entity model instance is created by cloning the model asset under a new
        // random id. If the instance still uses the original shared asset id, this component
        // connected to the unique-instance bus after the mesh was registered — re-set the asset
        // once so the mesh feature processor re-evaluates the clone callback.
        AZ::Data::AssetId originalId;
        AZ::Render::MeshComponentRequestBus::EventResult(
            originalId, GetEntityId(), &AZ::Render::MeshComponentRequestBus::Events::GetModelAssetId);
        if (model->GetModelAsset().GetId() == originalId)
        {
            if (!m_requestedModelRefresh)
            {
                m_requestedModelRefresh = true;
                // Queued: re-registering the mesh from inside the model-ready notification would re-enter
                // the mesh feature processor.
                AZ::TickBus::QueueFunction(
                    [entityId = GetEntityId()]()
                    {
                        AZ::Render::MeshComponentRequestBus::Event(
                            entityId, &AZ::Render::MeshComponentRequestBus::Events::RefreshModelRegistration);
                    });
            }
            return;
        }

        BuildSimulation(model);
    }

    void SoftBodyComponent::OnModelPreDestroy()
    {
        AZ::TickBus::Handler::BusDisconnect();
        ReleaseSimulation();
    }

    void SoftBodyComponent::BuildSimulation(const AZ::Data::Instance<AZ::RPI::Model>& model)
    {
        ReleaseSimulation();
        m_model = model;

        AZ::TransformBus::EventResult(m_worldTM, GetEntityId(), &AZ::TransformInterface::GetWorldTM);

        const auto lodAssets = model->GetModelAsset()->GetLodAssets();
        if (lodAssets.empty() || !lodAssets.front().IsReady())
        {
            return;
        }
        const AZ::Data::Asset<AZ::RPI::ModelLodAsset>& lodAsset = lodAssets.front();

        // Gather all submesh vertices (local space), welding coincident positions into particles.
        AZStd::unordered_map<uint64_t, uint32_t> weldMap;
        AZStd::vector<AZ::Vector3> particlePositions;
        const AZ::Name positionSemantic("POSITION");
        const AZ::Name normalSemantic("NORMAL");

        uint32_t subMeshIndex = 0;
        for (const AZ::RPI::ModelLodAsset::Mesh& mesh : lodAsset->GetMeshes())
        {
            const AZStd::span<const float> positions = mesh.GetSemanticBufferTyped<float>(positionSemantic);
            if (positions.empty())
            {
                ++subMeshIndex;
                continue;
            }
            const AZStd::span<const float> normals = mesh.GetSemanticBufferTyped<float>(normalSemantic);
            const uint32_t vertexCount = static_cast<uint32_t>(positions.size() / 3);

            SubMeshRange range;
            range.m_subMeshIndex = subMeshIndex;
            range.m_vertexCount = vertexCount;
            range.m_firstParticle = static_cast<uint32_t>(m_originalPositions.size());
            m_subMeshes.push_back(range);

            for (uint32_t v = 0; v < vertexCount; ++v)
            {
                const AZ::Vector3 localPosition(positions[3 * v], positions[3 * v + 1], positions[3 * v + 2]);
                m_originalPositions.push_back(localPosition);
                m_originalNormals.push_back(
                    normals.size() >= positions.size()
                        ? AZ::Vector3(normals[3 * v], normals[3 * v + 1], normals[3 * v + 2])
                        : AZ::Vector3::CreateAxisZ());

                const uint64_t key = PositionKey(localPosition);
                auto found = weldMap.find(key);
                if (found == weldMap.end())
                {
                    const uint32_t particleIndex = static_cast<uint32_t>(particlePositions.size());
                    weldMap.emplace(key, particleIndex);
                    particlePositions.push_back(localPosition);
                    m_weldedIndex.push_back(static_cast<int32_t>(particleIndex));
                }
                else
                {
                    m_weldedIndex.push_back(static_cast<int32_t>(found->second));
                }
            }

            // Triangles in welded space (offset local indices by the submesh base).
            const uint32_t base = range.m_firstParticle;
            auto appendIndices = [&](auto indices)
            {
                for (size_t i = 0; i + 2 < indices.size(); i += 3)
                {
                    const uint32_t i0 = static_cast<uint32_t>(indices[i]);
                    const uint32_t i1 = static_cast<uint32_t>(indices[i + 1]);
                    const uint32_t i2 = static_cast<uint32_t>(indices[i + 2]);
                    if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
                    {
                        continue;
                    }
                    m_weldedTriangles.push_back(static_cast<uint32_t>(m_weldedIndex[base + i0]));
                    m_weldedTriangles.push_back(static_cast<uint32_t>(m_weldedIndex[base + i1]));
                    m_weldedTriangles.push_back(static_cast<uint32_t>(m_weldedIndex[base + i2]));
                }
            };
            const uint32_t indexElementSize = mesh.GetIndexBufferAssetView().GetBufferViewDescriptor().m_elementSize;
            if (indexElementSize == sizeof(uint16_t))
            {
                appendIndices(mesh.GetIndexBufferTyped<uint16_t>());
            }
            else
            {
                appendIndices(mesh.GetIndexBufferTyped<uint32_t>());
            }

            ++subMeshIndex;
        }

        if (particlePositions.empty() || m_weldedTriangles.empty())
        {
            ReleaseSimulation();
            return;
        }

        // Simulate in world space.
        AZStd::vector<AZ::Vector3> worldPositions;
        worldPositions.reserve(particlePositions.size());
        for (const AZ::Vector3& localPosition : particlePositions)
        {
            worldPositions.push_back(m_worldTM.TransformPoint(localPosition));
        }

        AZ::SoftBodyConfig config;
        config.m_gravity = AZ::Vector3(0.0f, 0.0f, -9.81f * m_settings.m_gravityScale);
        config.m_substeps = m_settings.m_substeps;
        config.m_iterations = m_settings.m_iterations;
        config.m_damping = m_settings.m_damping;
        config.m_groundPlaneEnabled = m_settings.m_groundCollision;
        config.m_groundHeight = m_settings.m_groundHeight;
        config.m_groundFriction = m_settings.m_groundFriction;
        config.m_pressure = m_settings.m_pressure;
        m_softBody.BuildFromTriangleMesh(worldPositions, m_weldedTriangles, m_settings.m_massPerVertex, m_settings.m_compliance, config);

        // The contact thickness must cover the gaps between particles, or colliders (and the
        // particles of other soft bodies) sink into the faces between vertices before any particle
        // registers a contact. Grow it to half the average mesh edge length so the particle spheres
        // tile the surface without holes.
        float contactRadius = m_settings.m_particleRadius;
        if (m_settings.m_autoContactThickness)
        {
            const auto& edges = m_softBody.GetDistanceConstraints();
            if (!edges.empty())
            {
                float edgeLengthSum = 0.0f;
                for (const AZ::SoftBodyDistanceConstraint& edge : edges)
                {
                    edgeLengthSum += edge.m_restLength;
                }
                const float averageEdgeLength = edgeLengthSum / static_cast<float>(edges.size());
                contactRadius = AZStd::max(contactRadius, 0.5f * averageEdgeLength);
            }
        }

        const bool worldContacts = m_settings.m_collisionMode == SoftBodyCollisionMode::World ||
            m_settings.m_collisionMode == SoftBodyCollisionMode::WorldAndRigid;
        const bool softSoftContacts = m_settings.m_softSoftCollision;
        if (worldContacts || softSoftContacts)
        {
            WorldContactSettings contactSettings;
            contactSettings.m_particleRadius = contactRadius;
            contactSettings.m_friction = m_settings.m_worldFriction;
            contactSettings.m_rigidPushScale = m_settings.m_rigidPushScale;
            contactSettings.m_rigidMaxPushVelocity = m_settings.m_rigidMaxPushVelocity;
            contactSettings.m_includeRigidBodies = m_settings.m_collisionMode == SoftBodyCollisionMode::WorldAndRigid;
            contactSettings.m_selfEntityId = GetEntityId();
            m_softBody.SetCollisionSolver(
                [contactSettings, worldContacts, softSoftContacts,
                 body = &m_softBody,
                 particleRadius = contactRadius,
                 softSoftFriction = m_settings.m_softSoftFriction](AZStd::vector<AZ::SoftBodyParticle>& particles, float dt)
                {
                    if (worldContacts)
                    {
                        SolveWorldContacts(particles, dt, contactSettings);
                    }
                    if (softSoftContacts)
                    {
                        SoftBodyRegistry::SolveContacts(body, particleRadius, softSoftFriction);
                    }
                });
        }
        if (softSoftContacts)
        {
            SoftBodyRegistry::Register(&m_softBody, contactRadius);
        }

        if (m_settings.m_pinHighestVertices)
        {
            float maxZ = AZStd::numeric_limits<float>::lowest();
            for (const AZ::SoftBodyParticle& particle : m_softBody.GetParticles())
            {
                maxZ = AZStd::max(maxZ, static_cast<float>(particle.m_position.GetZ()));
            }
            for (AZ::SoftBodyParticle& particle : m_softBody.GetParticles())
            {
                if (particle.m_position.GetZ() >= maxZ - m_settings.m_pinTolerance)
                {
                    particle.m_invMass = 0.0f;
                }
            }
        }

        m_simulationReady = true;
        AZ::TickBus::Handler::BusConnect();
    }

    void SoftBodyComponent::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        if (!m_simulationReady)
        {
            return;
        }
        AZ::TransformBus::EventResult(m_worldTM, GetEntityId(), &AZ::TransformInterface::GetWorldTM);
        m_softBody.Step(AZStd::min(deltaTime, MaxTickDelta));
        WriteRenderData(/*restoreOriginal=*/false);
    }

    int SoftBodyComponent::GetTickOrder()
    {
        return AZ::TICK_PHYSICS;
    }

    void SoftBodyComponent::WriteRenderData(bool restoreOriginal)
    {
        if (!m_model)
        {
            return;
        }
        const AZ::Data::Asset<AZ::RPI::ModelAsset> modelAsset = m_model->GetModelAsset();
        if (!modelAsset.IsReady() || modelAsset->GetLodAssets().empty())
        {
            return;
        }
        const AZ::Data::Asset<AZ::RPI::ModelLodAsset>& lodAsset = modelAsset->GetLodAssets().front();
        const auto meshes = lodAsset->GetMeshes();

        // Deformed local-space positions and welded-space normals.
        AZStd::vector<AZ::Vector3> weldedNormals;
        AZ::Transform invTM = AZ::Transform::CreateIdentity();
        if (!restoreOriginal)
        {
            invTM = m_worldTM.GetInverse();

            weldedNormals.resize(m_softBody.GetParticles().size(), AZ::Vector3::CreateZero());
            const auto& particles = m_softBody.GetParticles();
            for (size_t i = 0; i + 2 < m_weldedTriangles.size(); i += 3)
            {
                const AZ::Vector3& a = particles[m_weldedTriangles[i]].m_position;
                const AZ::Vector3& b = particles[m_weldedTriangles[i + 1]].m_position;
                const AZ::Vector3& c = particles[m_weldedTriangles[i + 2]].m_position;
                const AZ::Vector3 faceNormal = (b - a).Cross(c - a); // area-weighted
                weldedNormals[m_weldedTriangles[i]] += faceNormal;
                weldedNormals[m_weldedTriangles[i + 1]] += faceNormal;
                weldedNormals[m_weldedTriangles[i + 2]] += faceNormal;
            }
            const AZ::Quaternion invRotation = invTM.GetRotation();
            for (AZ::Vector3& normal : weldedNormals)
            {
                normal = invRotation.TransformVector(normal);
                normal.NormalizeSafe();
            }
        }

        const AZ::Name positionSemantic("POSITION");
        const AZ::Name normalSemantic("NORMAL");
        for (const SubMeshRange& range : m_subMeshes)
        {
            if (range.m_subMeshIndex >= meshes.size())
            {
                continue;
            }
            const AZ::RPI::ModelLodAsset::Mesh& subMesh = meshes[range.m_subMeshIndex];
            MappedBuffer<AZ::PackedVector3f> destPositions(subMesh.GetSemanticBufferAssetView(positionSemantic), range.m_vertexCount);
            MappedBuffer<AZ::PackedVector3f> destNormals(subMesh.GetSemanticBufferAssetView(normalSemantic), range.m_vertexCount);
            const auto& destPositionsData = destPositions.GetBuffer();
            const auto& destNormalsData = destNormals.GetBuffer();
            if (destPositionsData.empty())
            {
                continue;
            }

            for (uint32_t v = 0; v < range.m_vertexCount; ++v)
            {
                const uint32_t vertexIndex = range.m_firstParticle + v;
                AZ::Vector3 localPosition;
                AZ::Vector3 localNormal;
                if (restoreOriginal)
                {
                    localPosition = m_originalPositions[vertexIndex];
                    localNormal = m_originalNormals[vertexIndex];
                }
                else
                {
                    const uint32_t particleIndex = static_cast<uint32_t>(m_weldedIndex[vertexIndex]);
                    localPosition = invTM.TransformPoint(m_softBody.GetParticles()[particleIndex].m_position);
                    localNormal = weldedNormals[particleIndex];
                }

                for (auto& [deviceIndex, buffer] : destPositionsData)
                {
                    buffer[v].Set(localPosition.GetX(), localPosition.GetY(), localPosition.GetZ());
                }
                if (!destNormalsData.empty())
                {
                    for (auto& [deviceIndex, buffer] : destNormalsData)
                    {
                        buffer[v].Set(localNormal.GetX(), localNormal.GetY(), localNormal.GetZ());
                    }
                }
            }
        }
    }

    void SoftBodyComponent::ReleaseSimulation()
    {
        SoftBodyRegistry::Unregister(&m_softBody);
        m_model = nullptr;
        m_subMeshes.clear();
        m_originalPositions.clear();
        m_originalNormals.clear();
        m_weldedIndex.clear();
        m_weldedTriangles.clear();
        m_softBody = AZ::SoftBody();
        m_simulationReady = false;
    }
} // namespace SoftBodyPhysics
