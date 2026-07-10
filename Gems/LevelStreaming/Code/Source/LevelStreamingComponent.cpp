/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "LevelStreamingComponent.h"

#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/containers/map.h>

#include <Atom/RPI.Public/ViewportContext.h>
#include <Atom/RPI.Public/ViewportContextBus.h>
#include <AtomLyIntegration/CommonFeatures/Mesh/MeshComponentBus.h>

namespace LevelStreaming
{
    LevelStreamingComponent::LevelStreamingComponent(const LevelStreamingSettings& settings)
        : m_settings(settings)
    {
    }

    void LevelStreamingComponent::Reflect(AZ::ReflectContext* context)
    {
        LevelStreamingSettings::Reflect(context);

        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<LevelStreamingComponent, AZ::Component>()
                ->Version(1)
                ->Field("Settings", &LevelStreamingComponent::m_settings);
        }
    }

    void LevelStreamingComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("LevelStreamingService"));
    }

    void LevelStreamingComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("LevelStreamingService"));
    }

    void LevelStreamingComponent::Activate()
    {
        m_timeUntilRebuild = 0.0f;
        AZ::TickBus::Handler::BusConnect();
    }

    void LevelStreamingComponent::Deactivate()
    {
        AZ::TickBus::Handler::BusDisconnect();
        RestoreAllHidden();
        m_chunks.clear();
    }

    void LevelStreamingComponent::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        m_timeUntilRebuild -= deltaTime;
        if (m_timeUntilRebuild <= 0.0f)
        {
            m_timeUntilRebuild = AZ::GetMax(m_settings.m_rebuildInterval, 0.05f);
            RebuildChunks();
        }

        auto* viewportContextInterface = AZ::Interface<AZ::RPI::ViewportContextRequestsInterface>::Get();
        if (!viewportContextInterface)
        {
            return;
        }
        AZ::RPI::ViewportContextPtr viewportContext = viewportContextInterface->GetDefaultViewportContext();
        if (!viewportContext)
        {
            return;
        }

        UpdateChunkStreaming(viewportContext->GetCameraTransform().GetTranslation());
    }

    void LevelStreamingComponent::RebuildChunks()
    {
        const float chunkSize = AZ::GetMax(m_settings.m_chunkSize, 1.0f);

        // Ordered map so chunk indices are stable between rebuilds for identical scenes.
        AZStd::map<AZStd::pair<int32_t, int32_t>, size_t> chunkLookup;
        AZStd::vector<Chunk> chunks;

        AZ::ComponentApplicationBus::Broadcast(
            &AZ::ComponentApplicationRequests::EnumerateEntities,
            [&](AZ::Entity* entity)
            {
                const AZ::EntityId entityId = entity->GetId();
                if (entityId == GetEntityId())
                {
                    return;
                }

                AZ::Aabb bounds = AZ::Aabb::CreateNull();
                AZ::Render::MeshComponentRequestBus::EventResult(
                    bounds, entityId, &AZ::Render::MeshComponentRequests::GetWorldBounds);
                if (!bounds.IsValid())
                {
                    return; // Not a mesh entity (or no model loaded yet).
                }

                // Leave meshes hidden by something other than this component alone.
                if (m_hiddenByStreaming.find(entityId) == m_hiddenByStreaming.end())
                {
                    bool visible = true;
                    AZ::Render::MeshComponentRequestBus::EventResult(
                        visible, entityId, &AZ::Render::MeshComponentRequests::GetVisibility);
                    if (!visible)
                    {
                        return;
                    }
                }

                const AZ::Vector3 center = bounds.GetCenter();
                const AZStd::pair<int32_t, int32_t> key(
                    aznumeric_cast<int32_t>(AZStd::floorf(center.GetX() / chunkSize)),
                    aznumeric_cast<int32_t>(AZStd::floorf(center.GetY() / chunkSize)));

                auto [it, inserted] = chunkLookup.emplace(key, chunks.size());
                if (inserted)
                {
                    chunks.emplace_back();
                }
                Chunk& chunk = chunks.at(it->second);
                chunk.m_bounds.AddAabb(bounds);
                chunk.m_members.push_back(entityId);
            });

        // Carry over active state so hysteresis survives rebuilds: a chunk stays in its previous
        // state if any of its members were streamed out before.
        for (Chunk& chunk : chunks)
        {
            chunk.m_active = true;
            for (const AZ::EntityId& member : chunk.m_members)
            {
                if (m_hiddenByStreaming.find(member) != m_hiddenByStreaming.end())
                {
                    chunk.m_active = false;
                    break;
                }
            }
        }

        m_chunks = AZStd::move(chunks);
    }

    void LevelStreamingComponent::UpdateChunkStreaming(const AZ::Vector3& cameraPosition)
    {
        const float streamIn = m_settings.m_streamDistance;
        const float streamOut = streamIn * (1.0f + AZ::GetMax(m_settings.m_hysteresis, 0.0f));
        const float streamInSq = streamIn * streamIn;
        const float streamOutSq = streamOut * streamOut;

        for (size_t chunkIndex = 0; chunkIndex < m_chunks.size(); ++chunkIndex)
        {
            Chunk& chunk = m_chunks[chunkIndex];
            const float distanceSq = chunk.m_bounds.GetDistanceSq(cameraPosition);

            if (chunk.m_active && distanceSq > streamOutSq)
            {
                chunk.m_active = false;
                SetChunkVisible(chunkIndex, false);
            }
            else if (!chunk.m_active && distanceSq < streamInSq)
            {
                chunk.m_active = true;
                SetChunkVisible(chunkIndex, true);
            }
        }
    }

    void LevelStreamingComponent::SetChunkVisible(size_t chunkIndex, bool visible)
    {
        for (const AZ::EntityId& member : m_chunks[chunkIndex].m_members)
        {
            AZ::Render::MeshComponentRequestBus::Event(
                member, &AZ::Render::MeshComponentRequests::SetVisibility, visible);
            if (visible)
            {
                m_hiddenByStreaming.erase(member);
            }
            else
            {
                m_hiddenByStreaming.insert(member);
            }
        }
    }

    void LevelStreamingComponent::RestoreAllHidden()
    {
        for (const AZ::EntityId& entityId : m_hiddenByStreaming)
        {
            AZ::Render::MeshComponentRequestBus::Event(
                entityId, &AZ::Render::MeshComponentRequests::SetVisibility, true);
        }
        m_hiddenByStreaming.clear();
    }
} // namespace LevelStreaming
