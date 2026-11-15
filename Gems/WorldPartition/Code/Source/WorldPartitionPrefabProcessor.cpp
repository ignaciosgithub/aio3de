/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "WorldPartitionPrefabProcessor.h"

#include "WorldPartitionComponent.h"
#include "WorldPartitionPolicyComponent.h"
#include "WorldPartitionStreamingSourceComponent.h"

#include <AzCore/Component/EntityUtils.h>
#include <AzCore/Debug/Trace.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/containers/unordered_set.h>
#include <AzCore/std/math.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzFramework/Components/TransformComponent.h>
#include <AzFramework/Spawnable/SpawnableAssetHandler.h>
#include <AzCore/std/limits.h>
#include <AzToolsFramework/Prefab/Instance/Instance.h>
#include <AzToolsFramework/Prefab/Spawnable/PrefabDocument.h>
#include <AzToolsFramework/Prefab/Spawnable/PrefabProcessorContext.h>
#include <map>
#include <set>

namespace WorldPartition
{
    using AzToolsFramework::Prefab::PrefabConversionUtils::PrefabProcessor;
    using AzToolsFramework::Prefab::PrefabConversionUtils::PrefabProcessorContext;
    using AzToolsFramework::Prefab::PrefabConversionUtils::PrefabDocument;
    using AzToolsFramework::Prefab::PrefabConversionUtils::EntityAliasSpawnableLoadBehavior;
    using PrefabInstance = AzToolsFramework::Prefab::Instance;

    namespace
    {
        using namespace AzToolsFramework::Prefab;

        struct CellKey
        {
            int32_t x, y, z;
            bool operator<(const CellKey& rhs) const
            {
                return x != rhs.x ? x < rhs.x : (y != rhs.y ? y < rhs.y : z < rhs.z);
            }
        };
        struct Candidate
        {
            AZ::Entity* m_root = nullptr;
            AZStd::vector<AZ::EntityId> m_entities;
            AZ::Transform m_world = AZ::Transform::CreateIdentity();
            CellKey m_key{};
        };

        AZ::Transform GetWorldTransform(
            const AZ::EntityId& id, const AZStd::unordered_map<AZ::EntityId, AZ::Entity*>& entities,
            AZStd::unordered_map<AZ::EntityId, AZ::Transform>& cache, AZStd::unordered_set<AZ::EntityId>& visiting)
        {
            if (auto cached = cache.find(id); cached != cache.end())
            {
                return cached->second;
            }
            auto found = entities.find(id);
            if (found == entities.end() || !visiting.insert(id).second)
            {
                return AZ::Transform::CreateIdentity();
            }
            auto* transform = found->second->FindComponent<AzFramework::TransformComponent>();
            AZ::Transform world = transform ? transform->GetLocalTM() : AZ::Transform::CreateIdentity();
            if (transform && transform->GetParentId().IsValid() && entities.contains(transform->GetParentId()))
            {
                world = GetWorldTransform(transform->GetParentId(), entities, cache, visiting) * world;
            }
            visiting.erase(id);
            cache.emplace(id, world);
            return world;
        }
    }

    void WorldPartitionPrefabProcessor::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<WorldPartitionPrefabProcessor, PrefabProcessor>()->Version(1);
        }
    }

    void WorldPartitionPrefabProcessor::Process(PrefabProcessorContext& context)
    {
        uint32_t levels = 0;
        uint32_t cellCount = 0;
        uint32_t streamedRoots = 0;
        AZStd::unordered_map<AZStd::string, uint32_t> kept;
        context.ListPrefabs([&](PrefabDocument& document)
        {
            PrefabInstance& rootInstance = document.GetInstance();
            WorldPartitionComponent* partition = nullptr;
            rootInstance.GetEntities([&](AZStd::unique_ptr<AZ::Entity>& entity)
            {
                if (entity)
                {
                    partition = entity->FindComponent<WorldPartitionComponent>();
                    if (partition)
                    {
                        return false;
                    }
                }
                return true;
            });
            if (!partition)
            {
                return;
            }
            ++levels;

            AZStd::unordered_map<AZ::EntityId, AZ::Entity*> entities;
            AZStd::unordered_map<AZ::EntityId, PrefabInstance*> owners;
            AZStd::unordered_map<PrefabInstance*, AZStd::unordered_set<AZ::EntityId>> owned;
            auto addEntity = [&](AZ::Entity& entity, PrefabInstance* owner)
            {
                entities[entity.GetId()] = &entity;
                owners[entity.GetId()] = owner;
                owned[owner].insert(entity.GetId());
            };

            rootInstance.GetEntities([&](AZStd::unique_ptr<AZ::Entity>& entity)
            {
                if (entity) { addEntity(*entity, &rootInstance); }
                return true;
            });
            if (auto container = rootInstance.GetContainerEntity(); container)
            {
                addEntity(container->get(), &rootInstance);
            }
            rootInstance.GetNestedInstances([&](AZStd::unique_ptr<PrefabInstance>& nested)
            {
                if (auto container = nested->GetContainerEntity(); container)
                {
                    addEntity(container->get(), nested.get());
                }
                nested->GetAllEntitiesInHierarchy([&](AZStd::unique_ptr<AZ::Entity>& entity)
                {
                    if (entity) { addEntity(*entity, nested.get()); }
                    return true;
                });
            });

            AZStd::unordered_map<AZ::EntityId, AZStd::vector<AZ::EntityId>> children;
            AZStd::unordered_map<AZ::EntityId, AZ::Transform> worldCache;
            AZStd::unordered_set<AZ::EntityId> visiting;
            for (const auto& [id, entity] : entities)
            {
                if (auto* transform = entity->FindComponent<AzFramework::TransformComponent>())
                {
                    if (transform->GetParentId().IsValid())
                    {
                        children[transform->GetParentId()].push_back(id);
                    }
                }
            }
            const AZ::EntityId containerId = rootInstance.GetContainerEntityId();
            AZStd::vector<Candidate> movable;
            for (const auto& [id, entity] : entities)
            {
                auto* transform = entity->FindComponent<AzFramework::TransformComponent>();
                if (!transform || transform->GetParentId() != containerId)
                {
                    continue;
                }
                Candidate candidate;
                candidate.m_root = entity;
                AZStd::vector<AZ::EntityId> pending{ id };
                AZStd::unordered_set<AZ::EntityId> subtree;
                while (!pending.empty())
                {
                    AZ::EntityId current = pending.back();
                    pending.pop_back();
                    if (!subtree.insert(current).second) { continue; }
                    candidate.m_entities.push_back(current);
                    if (auto found = children.find(current); found != children.end())
                    {
                        pending.insert(pending.end(), found->second.begin(), found->second.end());
                    }
                }

                AZStd::string reason = "ineligible";
                bool eligible = true;
                for (AZ::EntityId memberId : candidate.m_entities)
                {
                    AZ::Entity* member = entities[memberId];
                    if (!member->FindComponent<AzFramework::TransformComponent>())
                    {
                        eligible = false; reason = "missing-transform"; break;
                    }
                    if (auto* policy = member->FindComponent<WorldPartitionPolicyComponent>();
                        policy && policy->m_policy == WorldPartitionPolicy::AlwaysLoaded)
                    {
                        eligible = false; reason = "always-loaded"; break;
                    }
                    if (member->FindComponent<WorldPartitionComponent>() ||
                        member->FindComponent<WorldPartitionStreamingSourceComponent>())
                    {
                        eligible = false; reason = "partition-or-source-component"; break;
                    }
                    for (AZ::Component* component : member->GetComponents())
                    {
                        if (AZStd::string_view(component->RTTI_GetTypeName()) == "NetBindComponent")
                        {
                            eligible = false; reason = "networked"; break;
                        }
                    }
                    if (!eligible) { break; }
                }
                if (eligible && !partition->m_streamByDefault)
                {
                    auto* policy = entity->FindComponent<WorldPartitionPolicyComponent>();
                    if (!policy || policy->m_policy != WorldPartitionPolicy::Streamed)
                    {
                        eligible = false; reason = "not-opted-in";
                    }
                }
                if (eligible)
                {
                    for (AZ::EntityId memberId : candidate.m_entities)
                    {
                        PrefabInstance* owner = owners[memberId];
                        if (owner != &rootInstance)
                        {
                            for (AZ::EntityId ownedId : owned[owner])
                            {
                                if (!subtree.contains(ownedId))
                                {
                                    eligible = false; reason = "partial-nested-instance"; break;
                                }
                            }
                        }
                        if (!eligible) { break; }
                    }
                }
                if (eligible)
                {
                    for (const auto& entityEntry : entities)
                    {
                        const AZ::EntityId id = entityEntry.first;
                        AZ::Entity* entity = entityEntry.second;
                        for (AZ::Component* component : entity->GetComponents())
                        {
                            AZ::EntityUtils::EnumerateEntityIds(component, component->RTTI_GetType(),
                                [&](const AZ::EntityId& referencedId, bool, const AZ::SerializeContext::ClassElement*)
                                {
                                    if (!referencedId.IsValid() || referencedId == entity->GetId()) { return; }
                                    const bool sourceInside = subtree.contains(id);
                                    const bool targetInside = subtree.contains(referencedId);
                                    const bool rootTransformParent =
                                        component == candidate.m_root->FindComponent<AzFramework::TransformComponent>() &&
                                        id == candidate.m_root->GetId() && referencedId == containerId;
                                    if ((!sourceInside && targetInside) ||
                                        (sourceInside && !targetInside && !rootTransformParent))
                                    {
                                        eligible = false;
                                        reason = "cross-reference";
                                    }
                                });
                            if (!eligible) { break; }
                        }
                        if (!eligible) { break; }
                    }
                }
                if (!eligible)
                {
                    ++kept[reason];
                    continue;
                }

                candidate.m_world = GetWorldTransform(id, entities, worldCache, visiting);
                const float size = AZStd::max(partition->m_cellSize, 0.001f);
                candidate.m_key = {
                    static_cast<int32_t>(AZStd::floor(candidate.m_world.GetTranslation().GetX() / size)),
                    static_cast<int32_t>(AZStd::floor(candidate.m_world.GetTranslation().GetY() / size)),
                    partition->m_twoDimensional ? 0 : static_cast<int32_t>(AZStd::floor(candidate.m_world.GetTranslation().GetZ() / size)) };
                movable.push_back(AZStd::move(candidate));
            }

            std::map<CellKey, AZStd::vector<Candidate>> cells;
            for (Candidate& candidate : movable)
            {
                cells[candidate.m_key].push_back(AZStd::move(candidate));
            }
            partition->m_cells.clear();
            for (auto& [key, candidates] : cells)
            {
                ++cellCount;
                const AZStd::string cellName = AZStd::string::format("%s_wp_%d_%d_%d",
                    document.GetName().c_str(), key.x, key.y, key.z);
                PrefabDocument cellDocument(cellName);
                PrefabInstance& cellInstance = cellDocument.GetInstance();
                AZ::Vector3 minimum = AZ::Vector3(AZStd::numeric_limits<float>::max());
                AZ::Vector3 maximum = AZ::Vector3(AZStd::numeric_limits<float>::lowest());
                uint32_t entityCount = 0;
                AZStd::unordered_set<PrefabInstance*> nestedToMove;

                for (Candidate& candidate : candidates)
                {
                    ++streamedRoots;
                    AZ::Entity* root = candidate.m_root;
                    auto* rootTransform = root->FindComponent<AzFramework::TransformComponent>();
                    rootTransform->SetParent(AZ::EntityId());
                    rootTransform->SetLocalTM(candidate.m_world);
                    for (AZ::EntityId memberId : candidate.m_entities)
                    {
                        const AZ::Vector3 position = worldCache.contains(memberId)
                            ? worldCache[memberId].GetTranslation() : GetWorldTransform(memberId, entities, worldCache, visiting).GetTranslation();
                        minimum = minimum.GetMin(position);
                        maximum = maximum.GetMax(position);
                        ++entityCount;
                        if (owners[memberId] != &rootInstance)
                        {
                            nestedToMove.insert(owners[memberId]);
                        }
                        else if (memberId != rootInstance.GetContainerEntityId())
                        {
                            AZStd::unique_ptr<AZ::Entity> detached = rootInstance.DetachEntity(memberId);
                            if (detached) { cellInstance.AddEntity(AZStd::move(detached)); }
                        }
                    }
                }
                for (PrefabInstance* nested : nestedToMove)
                {
                    AZStd::unique_ptr<PrefabInstance> detached = rootInstance.DetachNestedInstance(nested->GetInstanceAlias());
                    if (detached) { cellInstance.AddInstance(AZStd::move(detached)); }
                }
                WorldPartitionCell cell;
                cell.m_x = key.x; cell.m_y = key.y; cell.m_z = key.z; cell.m_entityCount = entityCount;
                const float size = AZStd::max(partition->m_cellSize, 0.001f);
                float minZ = partition->m_twoDimensional ? minimum.GetZ() : key.z * size;
                float maxZ = partition->m_twoDimensional ? maximum.GetZ() : (key.z + 1) * size;
                cell.m_bounds = AZ::Aabb::CreateFromMinMax(
                    AZ::Vector3(key.x * size, key.y * size, minZ),
                    AZ::Vector3((key.x + 1) * size, (key.y + 1) * size, maxZ));
                AZStd::string productName = cellName + AzFramework::Spawnable::DotFileExtension;
                cell.m_spawnable = AZ::Data::Asset<AzFramework::Spawnable>(
                    AZ::Data::AssetId(context.GetSourceUuid(), AzFramework::SpawnableAssetHandler::BuildSubId(productName)),
                    azrtti_typeid<AzFramework::Spawnable>());
                cell.m_spawnable.SetAutoLoadBehavior(AZ::Data::AssetLoadBehavior::NoLoad);
                partition->m_cells.push_back(cell);
                context.RegisterSpawnableProductAssetDependency(
                    document.GetName(), cellName, EntityAliasSpawnableLoadBehavior::NoLoad);
                context.AddPrefab(AZStd::move(cellDocument));
            }
        });

        uint32_t keptCount = 0;
        AZStd::string reasons;
        for (const auto& [reason, count] : kept)
        {
            keptCount += count;
            reasons += AZStd::string::format("%s%s:%u", reasons.empty() ? "" : ",", reason.c_str(), count);
        }
        AZ_Printf("WorldPartition", "prefabs=%u cells=%u streamedRoots=%u keptRoots=%u reasons={%s}\n",
            levels, cellCount, streamedRoots, keptCount, reasons.c_str());
    }
}
