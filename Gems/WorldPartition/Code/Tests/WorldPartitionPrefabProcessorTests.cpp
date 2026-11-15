/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "WorldPartitionComponent.h"
#include "WorldPartitionPolicyComponent.h"
#include "WorldPartitionPrefabProcessor.h"

#include <AzFramework/Components/TransformComponent.h>
#include <AzFramework/Spawnable/SpawnableAssetHandler.h>
#include <AzToolsFramework/Prefab/PrefabDomTypes.h>
#include <AzToolsFramework/Prefab/Instance/Instance.h>
#include <AzToolsFramework/Prefab/PrefabSystemComponentInterface.h>
#include <AzToolsFramework/Prefab/Spawnable/PrefabCatchmentProcessor.h>
#include <AzToolsFramework/Prefab/Spawnable/PrefabDocument.h>
#include <AzToolsFramework/Prefab/Spawnable/PrefabProcessorContext.h>
#include <AzToolsFramework/UnitTest/AzToolsFrameworkTestHelpers.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/containers/unordered_set.h>
#include <AzCore/std/optional.h>
#include <algorithm>

namespace UnitTest
{
    using PrefabInstance = AzToolsFramework::Prefab::Instance;

    class EntityReferenceTestComponent : public AZ::Component
    {
    public:
        AZ_COMPONENT(EntityReferenceTestComponent, "{55E3986A-9AA0-49D8-96BE-0CE7EF129EF1}");
        void Activate() override {}
        void Deactivate() override {}
        static void Reflect(AZ::ReflectContext* context)
        {
            if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
            {
                serialize->Class<EntityReferenceTestComponent, AZ::Component>()
                    ->Version(1)->Field("Target", &EntityReferenceTestComponent::m_target);
            }
        }
        AZ::EntityId m_target;
    };

    class WorldPartitionPrefabProcessorTests : public ToolsApplicationFixture<false>
    {
    public:
        void SetUpEditorFixtureImpl() override
        {
            GetApplication()->RegisterComponentDescriptor(WorldPartition::WorldPartitionComponent::CreateDescriptor());
            GetApplication()->RegisterComponentDescriptor(WorldPartition::WorldPartitionPolicyComponent::CreateDescriptor());
            GetApplication()->RegisterComponentDescriptor(EntityReferenceTestComponent::CreateDescriptor());
        }

        static AZ::Entity* AddEntity(
            AZStd::vector<AZ::Entity*>& entities, AZ::EntityId parent, const char* name, float x,
            WorldPartition::WorldPartitionPolicy policy = WorldPartition::WorldPartitionPolicy::Default,
            bool partition = false)
        {
            auto* entity = aznew AZ::Entity(name);
            auto* transform = entity->CreateComponent<AzFramework::TransformComponent>();
            transform->SetParent(parent);
            transform->SetLocalTM(AZ::Transform::CreateTranslation(AZ::Vector3(x, 0.0f, 0.0f)));
            if (partition)
            {
                entity->CreateComponent<WorldPartition::WorldPartitionComponent>();
            }
            else if (policy != WorldPartition::WorldPartitionPolicy::Default)
            {
                auto* policyComponent = entity->CreateComponent<WorldPartition::WorldPartitionPolicyComponent>();
                policyComponent->m_policy = policy;
            }
            entities.push_back(entity);
            return entity;
        }

        static AZStd::unique_ptr<PrefabInstance> CreatePrefabInstance(
            const AZStd::string& name, const AZStd::vector<AZ::Entity*>& entities, AZ::Entity* container,
            AZStd::vector<AZStd::unique_ptr<PrefabInstance>> nestedInstances = {})
        {
            auto* prefabSystem = AZ::Interface<AzToolsFramework::Prefab::PrefabSystemComponentInterface>::Get();
            if (!prefabSystem)
            {
                return {};
            }
            const AZStd::string path = AZStd::string::format("WorldPartitionTests/%s.prefab", name.c_str());
            return prefabSystem->CreatePrefab(
                entities, AZStd::move(nestedInstances), path.c_str(), AZStd::unique_ptr<AZ::Entity>(container));
        }

        static AZStd::optional<AzToolsFramework::Prefab::PrefabConversionUtils::PrefabDocument> CreatePrefabDocument(
            const AZStd::string& name, const AZStd::vector<AZ::Entity*>& entities, AZ::Entity* container,
            AZStd::vector<AZStd::unique_ptr<PrefabInstance>> nestedInstances = {})
        {
            auto* prefabSystem = AZ::Interface<AzToolsFramework::Prefab::PrefabSystemComponentInterface>::Get();
            AZStd::unique_ptr<PrefabInstance> sourceInstance =
                CreatePrefabInstance(name, entities, container, AZStd::move(nestedInstances));
            if (!prefabSystem || !sourceInstance)
            {
                return AZStd::nullopt;
            }
            AzToolsFramework::Prefab::PrefabDom prefabDom;
            prefabDom.CopyFrom(prefabSystem->FindTemplateDom(sourceInstance->GetTemplateId()), prefabDom.GetAllocator());
            AzToolsFramework::Prefab::PrefabConversionUtils::PrefabDocument document(name);
            if (!document.SetPrefabDom(AZStd::move(prefabDom)))
            {
                return AZStd::nullopt;
            }
            return AZStd::optional<AzToolsFramework::Prefab::PrefabConversionUtils::PrefabDocument>(AZStd::move(document));
        }

        static AZ::Entity* FindEntity(PrefabInstance& instance, const char* name)
        {
            AZ::Entity* result = nullptr;
            instance.GetEntities([&](AZStd::unique_ptr<AZ::Entity>& entity)
            {
                if (entity && entity->GetName() == name)
                {
                    result = entity.get();
                }
                return true;
            });
            return result;
        }
    };

    TEST_F(WorldPartitionPrefabProcessorTests, ExtractsGridCellsAndPreservesSubtreeTransforms)
    {
        using namespace AzToolsFramework::Prefab::PrefabConversionUtils;
        using namespace WorldPartition;
        const AZ::Uuid sourceUuid = AZ::Uuid::CreateRandom();
        PrefabProcessorContext context(sourceUuid);
        AZStd::vector<AZ::Entity*> entities;
        auto* container = aznew AZ::Entity("Container");
        container->CreateComponent<AzFramework::TransformComponent>();
        const AZ::EntityId containerId = container->GetId();

        AddEntity(entities, containerId, "Partition", 0.0f,
            WorldPartitionPolicy::Default, true);
        AddEntity(entities, containerId, "AtZero", 0.0f);
        AddEntity(entities, containerId, "AtTen", 10.0f);
        AZ::Entity* farRoot = AddEntity(entities, containerId, "AtTwoHundred", 200.0f);
        AddEntity(entities, farRoot->GetId(), "Child", 5.0f);
        AddEntity(entities, containerId, "Always", 20.0f, WorldPartitionPolicy::AlwaysLoaded);
        auto level = CreatePrefabDocument("Level", entities, container);
        ASSERT_TRUE(level.has_value());
        auto* partitionEntity = FindEntity(level->GetInstance(), "Partition");
        ASSERT_NE(partitionEntity, nullptr);
        auto* partition = partitionEntity->FindComponent<WorldPartitionComponent>();
        ASSERT_NE(partition, nullptr);
        partition->m_cellSize = 64.0f;
        partition->m_loadRadius = 128.0f;
        partition->m_unloadRadius = 160.0f;
        farRoot = FindEntity(level->GetInstance(), "AtTwoHundred");
        ASSERT_NE(farRoot, nullptr);
        AZ::Entity* child = FindEntity(level->GetInstance(), "Child");
        ASSERT_NE(child, nullptr);
        auto* childTransform = child->FindComponent<AzFramework::TransformComponent>();
        const AZ::Transform childLocal = childTransform->GetLocalTM();
        auto* alwaysLoaded = FindEntity(level->GetInstance(), "Always");
        ASSERT_NE(alwaysLoaded, nullptr);
        const AZ::EntityId alwaysLoadedId = alwaysLoaded->GetId();
        ASSERT_TRUE(context.AddPrefab(AZStd::move(*level)));

        WorldPartitionPrefabProcessor processor;
        processor.Process(context);

        EXPECT_EQ(partition->m_cells.size(), 2);
        ASSERT_EQ(partition->m_cells.size(), 2);
        EXPECT_EQ(partition->m_cells[0].m_x, 0);
        EXPECT_EQ(partition->m_cells[0].m_entityCount, 2);
        EXPECT_EQ(partition->m_cells[0].m_bounds.GetMin().GetX(), 0.0f);
        EXPECT_EQ(partition->m_cells[0].m_bounds.GetMax().GetX(), 64.0f);
        EXPECT_EQ(partition->m_cells[0].m_bounds.GetMin().GetZ(), 0.0f);
        EXPECT_EQ(partition->m_cells[0].m_bounds.GetMax().GetZ(), 0.0f);
        EXPECT_EQ(partition->m_cells[1].m_x, 3);
        EXPECT_EQ(partition->m_cells[1].m_entityCount, 2);
        EXPECT_EQ(partition->m_cells[0].m_spawnable.GetAutoLoadBehavior(), AZ::Data::AssetLoadBehavior::NoLoad);
        EXPECT_EQ(partition->m_cells[0].m_spawnable.GetId().m_subId,
            AzFramework::SpawnableAssetHandler::BuildSubId("Level_wp_0_0_0.spawnable"));

        auto* farTransform = farRoot->FindComponent<AzFramework::TransformComponent>();
        EXPECT_FALSE(farTransform->GetParentId().IsValid());
        EXPECT_EQ(farTransform->GetWorldTM().GetTranslation().GetX(), 200.0f);
        EXPECT_EQ(childTransform->GetLocalTM(), childLocal);
        bool alwaysRemains = false;
        AZStd::unordered_set<AZStd::string> remainingNames;
        PrefabDocument* levelDocument = nullptr;
        AZStd::unordered_set<AZStd::string> cellEntityNames;
        context.ListPrefabs([&](PrefabDocument& document)
        {
            if (document.GetName() == "Level")
            {
                levelDocument = &document;
                document.GetInstance().GetEntities([&](AZStd::unique_ptr<AZ::Entity>& entity)
                {
                    alwaysRemains |= entity && entity->GetId() == alwaysLoadedId;
                    if (entity) { remainingNames.emplace(entity->GetName()); }
                    return true;
                });
            }
            else
            {
                document.GetInstance().GetAllEntitiesInHierarchy([&](AZStd::unique_ptr<AZ::Entity>& entity)
                {
                    if (entity) { cellEntityNames.emplace(entity->GetName()); }
                    return true;
                });
            }
        });
        EXPECT_TRUE(alwaysRemains);
        EXPECT_FALSE(remainingNames.contains("AtZero"));
        EXPECT_FALSE(remainingNames.contains("AtTen"));
        EXPECT_FALSE(remainingNames.contains("AtTwoHundred"));
        EXPECT_TRUE(cellEntityNames.contains("AtZero"));
        EXPECT_TRUE(cellEntityNames.contains("AtTen"));
        EXPECT_TRUE(cellEntityNames.contains("AtTwoHundred"));
        EXPECT_TRUE(cellEntityNames.contains("Child"));
        ASSERT_NE(levelDocument, nullptr);

        AzToolsFramework::Prefab::PrefabConversionUtils::PrefabCatchmentProcessor catchment;
        catchment.Process(context);
        AZStd::vector<AZ::Data::AssetId> productIds;
        for (const auto& product : context.GetProcessedObjects())
        {
            productIds.push_back(product.GetAsset().GetId());
        }
        for (const WorldPartitionCell& cell : partition->m_cells)
        {
            EXPECT_NE(std::find(productIds.begin(), productIds.end(), cell.m_spawnable.GetId()), productIds.end());
        }
    }

    TEST_F(WorldPartitionPrefabProcessorTests, StreamByDefaultFalseOnlyMovesStreamedRoots)
    {
        using namespace AzToolsFramework::Prefab::PrefabConversionUtils;
        using namespace WorldPartition;
        PrefabProcessorContext context(AZ::Uuid::CreateRandom());
        AZStd::vector<AZ::Entity*> entities;
        auto* container = aznew AZ::Entity("Container");
        container->CreateComponent<AzFramework::TransformComponent>();
        const AZ::EntityId containerId = container->GetId();
        AddEntity(entities, containerId, "Partition", 0.0f,
            WorldPartitionPolicy::Default, true);
        AddEntity(entities, containerId, "Default", 0.0f);
        AddEntity(entities, containerId, "Streamed", 100.0f, WorldPartitionPolicy::Streamed);
        auto level = CreatePrefabDocument("OptIn", entities, container);
        ASSERT_TRUE(level.has_value());
        auto* partitionEntity = FindEntity(level->GetInstance(), "Partition");
        ASSERT_NE(partitionEntity, nullptr);
        auto* partition = partitionEntity->FindComponent<WorldPartitionComponent>();
        partition->m_streamByDefault = false;
        const AZ::EntityId defaultId = FindEntity(level->GetInstance(), "Default")->GetId();
        ASSERT_TRUE(context.AddPrefab(AZStd::move(*level)));

        WorldPartitionPrefabProcessor{}.Process(context);
        ASSERT_EQ(partition->m_cells.size(), 1);
        EXPECT_EQ(partition->m_cells[0].m_x, 1);
        EXPECT_EQ(partition->m_cells[0].m_entityCount, 1);
        bool defaultRemains = false;
        context.ListPrefabs([&](PrefabDocument& document)
        {
            if (document.GetName() == "OptIn")
            {
                document.GetInstance().GetEntities([&](AZStd::unique_ptr<AZ::Entity>& entity)
                {
                    defaultRemains |= entity && entity->GetId() == defaultId;
                    return true;
                });
            }
        });
        EXPECT_TRUE(defaultRemains);
    }

    TEST_F(WorldPartitionPrefabProcessorTests, KeepsRootsWithCrossBoundaryEntityReferences)
    {
        using namespace AzToolsFramework::Prefab::PrefabConversionUtils;
        using namespace WorldPartition;
        PrefabProcessorContext context(AZ::Uuid::CreateRandom());
        AZStd::vector<AZ::Entity*> entities;
        auto* container = aznew AZ::Entity("Container");
        container->CreateComponent<AzFramework::TransformComponent>();
        const AZ::EntityId containerId = container->GetId();
        AddEntity(entities, containerId, "Partition", 0.0f,
            WorldPartitionPolicy::Default, true);
        AZ::Entity* source = AddEntity(entities, containerId, "ReferenceSource", 0.0f);
        AZ::Entity* target = AddEntity(entities, containerId, "ReferenceTarget", 10.0f);
        auto* reference = source->CreateComponent<EntityReferenceTestComponent>();
        reference->m_target = target->GetId();
        auto level = CreatePrefabDocument("References", entities, container);
        ASSERT_TRUE(level.has_value());
        auto* partitionEntity = FindEntity(level->GetInstance(), "Partition");
        ASSERT_NE(partitionEntity, nullptr);
        auto* partition = partitionEntity->FindComponent<WorldPartitionComponent>();
        ASSERT_TRUE(context.AddPrefab(AZStd::move(*level)));

        WorldPartitionPrefabProcessor{}.Process(context);
        EXPECT_TRUE(partition->m_cells.empty());
    }

    TEST_F(WorldPartitionPrefabProcessorTests, MovesNestedPrefabInstanceAsOneCellSubtree)
    {
        using namespace AzToolsFramework::Prefab;
        using namespace AzToolsFramework::Prefab::PrefabConversionUtils;
        using namespace WorldPartition;
        PrefabProcessorContext context(AZ::Uuid::CreateRandom());
        AZStd::vector<AZ::Entity*> entities;
        auto* container = aznew AZ::Entity("Container");
        container->CreateComponent<AzFramework::TransformComponent>();
        const AZ::EntityId containerId = container->GetId();
        AddEntity(entities, containerId, "Partition", 0.0f,
            WorldPartitionPolicy::Default, true);

        AZStd::vector<AZ::Entity*> nestedEntities;
        auto* nestedContainer = aznew AZ::Entity("NestedContainer");
        auto* nestedTransform = nestedContainer->CreateComponent<AzFramework::TransformComponent>();
        nestedTransform->SetParent(containerId);
        nestedTransform->SetLocalTM(AZ::Transform::CreateTranslation(AZ::Vector3(70.0f, 0.0f, 0.0f)));
        auto* nestedChild = aznew AZ::Entity("NestedChild");
        auto* nestedChildTransform = nestedChild->CreateComponent<AzFramework::TransformComponent>();
        nestedChildTransform->SetParent(nestedContainer->GetId());
        nestedChildTransform->SetLocalTM(AZ::Transform::CreateTranslation(AZ::Vector3(2.0f, 0.0f, 0.0f)));
        nestedEntities.push_back(nestedChild);
        AZStd::unique_ptr<PrefabInstance> nested = CreatePrefabInstance("NestedSubprefab", nestedEntities, nestedContainer);
        ASSERT_NE(nested, nullptr);
        AZStd::vector<AZStd::unique_ptr<PrefabInstance>> nestedInstances;
        nestedInstances.push_back(AZStd::move(nested));
        auto level = CreatePrefabDocument("Nested", entities, container, AZStd::move(nestedInstances));
        ASSERT_TRUE(level.has_value());
        auto* partitionEntity = FindEntity(level->GetInstance(), "Partition");
        ASSERT_NE(partitionEntity, nullptr);
        auto* partition = partitionEntity->FindComponent<WorldPartitionComponent>();
        const AZ::EntityId levelContainerId = level->GetInstance().GetContainerEntityId();
        AZ::EntityId nestedContainerParent;
        level->GetInstance().GetNestedInstances([&](AZStd::unique_ptr<PrefabInstance>& nestedInstance)
        {
            if (auto nestedContainer = nestedInstance->GetContainerEntity(); nestedContainer)
            {
                if (auto* transform = nestedContainer->get().FindComponent<AzFramework::TransformComponent>())
                {
                    transform->SetParent(levelContainerId);
                    nestedContainerParent = transform->GetParentId();
                }
            }
        });
        EXPECT_EQ(nestedContainerParent, levelContainerId);
        ASSERT_TRUE(context.AddPrefab(AZStd::move(*level)));

        WorldPartitionPrefabProcessor{}.Process(context);
        ASSERT_EQ(partition->m_cells.size(), 1);
        EXPECT_EQ(partition->m_cells[0].m_x, 1);
        EXPECT_EQ(partition->m_cells[0].m_entityCount, 2);
        bool nestedInstanceMoved = false;
        context.ListPrefabs([&](PrefabDocument& document)
        {
            if (document.GetName() == "Nested_wp_1_0_0")
            {
                document.GetInstance().GetNestedInstances([&](AZStd::unique_ptr<PrefabInstance>&)
                {
                    nestedInstanceMoved = true;
                });
            }
        });
        EXPECT_TRUE(nestedInstanceMoved);
    }
}
