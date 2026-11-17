/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <BehaviorTreeComponent.h>
#include <BehaviorTreePrefabProcessor.h>

#include <AzTest/AzTest.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/optional.h>
#include <AzCore/std/utility/move.h>
#include <AzFramework/Components/TransformComponent.h>
#include <AzFramework/Spawnable/Spawnable.h>
#include <AzFramework/Spawnable/SpawnableAssetHandler.h>
#include <AzToolsFramework/Prefab/PrefabSystemComponentInterface.h>
#include <AzToolsFramework/Prefab/Spawnable/PrefabCatchmentProcessor.h>
#include <AzToolsFramework/Prefab/Spawnable/PrefabDocument.h>
#include <AzToolsFramework/Prefab/Spawnable/PrefabProcessorContext.h>
#include <AzToolsFramework/UnitTest/AzToolsFrameworkTestHelpers.h>

#include <algorithm>

namespace UnitTest
{
    using AzToolsFramework::Prefab::Instance;
    using AzToolsFramework::Prefab::PrefabConversionUtils::PrefabDocument;
    using AzToolsFramework::Prefab::PrefabConversionUtils::PrefabProcessorContext;

    class BehaviorTreePrefabProcessorTests : public ToolsApplicationFixture<false>
    {
    public:
        void SetUpEditorFixtureImpl() override
        {
            GetApplication()->RegisterComponentDescriptor(BehaviorTree::BehaviorTreeComponent::CreateDescriptor());
        }

        static AZStd::unique_ptr<Instance> CreatePrefabInstance(
            const AZStd::string& name, const AZStd::vector<AZ::Entity*>& entities, AZ::Entity* container)
        {
            auto* prefabSystem = AZ::Interface<AzToolsFramework::Prefab::PrefabSystemComponentInterface>::Get();
            if (!prefabSystem)
            {
                return {};
            }
            const AZStd::string path = AZStd::string::format("BehaviorTreeTests/%s.prefab", name.c_str());
            AZStd::vector<AZStd::unique_ptr<Instance>> nestedInstances;
            return prefabSystem->CreatePrefab(
                entities, AZStd::move(nestedInstances), path.c_str(), AZStd::unique_ptr<AZ::Entity>(container));
        }

        static AZStd::optional<PrefabDocument> CreatePrefabDocument(
            const AZStd::string& name, const AZStd::vector<AZ::Entity*>& entities, AZ::Entity* container)
        {
            auto* prefabSystem = AZ::Interface<AzToolsFramework::Prefab::PrefabSystemComponentInterface>::Get();
            AZStd::unique_ptr<Instance> sourceInstance = CreatePrefabInstance(name, entities, container);
            if (!prefabSystem || !sourceInstance)
            {
                return AZStd::nullopt;
            }

            AzToolsFramework::Prefab::PrefabDom prefabDom;
            prefabDom.CopyFrom(prefabSystem->FindTemplateDom(sourceInstance->GetTemplateId()), prefabDom.GetAllocator());
            PrefabDocument document(name);
            if (!document.SetPrefabDom(AZStd::move(prefabDom)))
            {
                return AZStd::nullopt;
            }
            return AZStd::optional<PrefabDocument>(AZStd::move(document));
        }

        static AZ::Entity* AddTreeEntity(
            AZStd::vector<AZ::Entity*>& entities, const AZ::EntityId& parent, const char* name, const char* treeFile)
        {
            auto* entity = aznew AZ::Entity(name);
            auto* transform = entity->CreateComponent<AzFramework::TransformComponent>();
            transform->SetParent(parent);
            auto* component = entity->CreateComponent<BehaviorTree::BehaviorTreeComponent>();
            component->SetConfiguration(treeFile, 0.0f, true, false);
            entities.push_back(entity);
            return entity;
        }

        static AZ::Data::AssetId GetLevelSpawnableAssetId(const AZ::Uuid& sourceUuid, const char* prefabName)
        {
            const AZStd::string productName =
                AZStd::string(prefabName) + AzFramework::Spawnable::DotFileExtension;
            return AZ::Data::AssetId(sourceUuid, AzFramework::SpawnableAssetHandler::BuildSubId(productName));
        }

        static void ExpectSingleNoLoadDependency(
            const PrefabProcessorContext& context, const AZ::Data::AssetId& spawnableAssetId,
            const AZ::Data::AssetId& treeAssetId)
        {
            const auto& dependencies = context.GetRegisteredProductAssetDependencies();
            ASSERT_EQ(dependencies.size(), 1);
            ASSERT_EQ(dependencies.begin()->first, spawnableAssetId);
            EXPECT_EQ(dependencies.begin()->second.m_assetId, treeAssetId);
            EXPECT_EQ(dependencies.begin()->second.m_loadBehavior, AZ::Data::AssetLoadBehavior::NoLoad);
        }
    };

    TEST_F(BehaviorTreePrefabProcessorTests, RegistersNoLoadDependencyFromPrefabSpawnable)
    {
        const AZ::Uuid sourceUuid = AZ::Uuid::CreateRandom();
        PrefabProcessorContext context(sourceUuid);
        AZStd::vector<AZ::Entity*> entities;
        auto* container = aznew AZ::Entity("Container");
        const AZ::EntityId containerId = container->GetId();
        container->CreateComponent<AzFramework::TransformComponent>();
        AddTreeEntity(entities, containerId, "TreeEntity", "AI/Guard.json");

        auto prefab = CreatePrefabDocument("TreeLevel", entities, container);
        ASSERT_TRUE(prefab.has_value());
        ASSERT_TRUE(context.AddPrefab(AZStd::move(*prefab)));

        const AZ::Data::AssetId treeAssetId(AZ::Uuid::CreateRandom(), 7);
        BehaviorTree::BehaviorTreePrefabProcessor processor;
        AZStd::string lookedUpPath;
        processor.m_treeAssetIdLookup = [&treeAssetId, &lookedUpPath](const AZStd::string& path)
        {
            lookedUpPath = path;
            return treeAssetId;
        };
        processor.Process(context);

        EXPECT_EQ(lookedUpPath, "AI/Guard.json");
        const AZ::Data::AssetId spawnableAssetId = GetLevelSpawnableAssetId(sourceUuid, "TreeLevel");
        ExpectSingleNoLoadDependency(context, spawnableAssetId, treeAssetId);

        AzToolsFramework::Prefab::PrefabConversionUtils::PrefabCatchmentProcessor catchment;
        catchment.Process(context);
        bool spawnableProductFound = false;
        for (const auto& product : context.GetProcessedObjects())
        {
            spawnableProductFound |= product.GetAsset().GetId() == spawnableAssetId;
        }
        EXPECT_TRUE(spawnableProductFound);
    }

    TEST_F(BehaviorTreePrefabProcessorTests, SharedTreePathRegistersOneDependency)
    {
        const AZ::Uuid sourceUuid = AZ::Uuid::CreateRandom();
        PrefabProcessorContext context(sourceUuid);
        AZStd::vector<AZ::Entity*> entities;
        auto* container = aznew AZ::Entity("Container");
        const AZ::EntityId containerId = container->GetId();
        container->CreateComponent<AzFramework::TransformComponent>();
        AddTreeEntity(entities, containerId, "FirstTreeEntity", "AI/Guard.json");
        AddTreeEntity(entities, containerId, "SecondTreeEntity", "AI/Guard.json");

        auto prefab = CreatePrefabDocument("SharedTreeLevel", entities, container);
        ASSERT_TRUE(prefab.has_value());
        ASSERT_TRUE(context.AddPrefab(AZStd::move(*prefab)));

        const AZ::Data::AssetId treeAssetId(AZ::Uuid::CreateRandom(), 7);
        BehaviorTree::BehaviorTreePrefabProcessor processor;
        size_t lookupCount = 0;
        processor.m_treeAssetIdLookup = [&treeAssetId, &lookupCount](const AZStd::string&)
        {
            ++lookupCount;
            return treeAssetId;
        };
        processor.Process(context);

        EXPECT_EQ(lookupCount, 1);
        ExpectSingleNoLoadDependency(context, GetLevelSpawnableAssetId(sourceUuid, "SharedTreeLevel"), treeAssetId);
    }

    TEST_F(BehaviorTreePrefabProcessorTests, EmptyTreePathRegistersNoDependency)
    {
        PrefabProcessorContext context(AZ::Uuid::CreateRandom());
        AZStd::vector<AZ::Entity*> entities;
        auto* container = aznew AZ::Entity("Container");
        const AZ::EntityId containerId = container->GetId();
        container->CreateComponent<AzFramework::TransformComponent>();
        AddTreeEntity(entities, containerId, "EmptyTreeEntity", "");

        auto prefab = CreatePrefabDocument("EmptyTreeLevel", entities, container);
        ASSERT_TRUE(prefab.has_value());
        ASSERT_TRUE(context.AddPrefab(AZStd::move(*prefab)));

        BehaviorTree::BehaviorTreePrefabProcessor processor;
        size_t lookupCount = 0;
        processor.m_treeAssetIdLookup = [&lookupCount](const AZStd::string&)
        {
            ++lookupCount;
            return AZ::Data::AssetId{};
        };
        processor.Process(context);

        EXPECT_EQ(lookupCount, 0);
        EXPECT_TRUE(context.GetRegisteredProductAssetDependencies().empty());
    }

    TEST_F(BehaviorTreePrefabProcessorTests, InvalidTreeAssetIdRegistersNoDependencyOrError)
    {
        PrefabProcessorContext context(AZ::Uuid::CreateRandom());
        AZStd::vector<AZ::Entity*> entities;
        auto* container = aznew AZ::Entity("Container");
        const AZ::EntityId containerId = container->GetId();
        container->CreateComponent<AzFramework::TransformComponent>();
        AddTreeEntity(entities, containerId, "InvalidTreeEntity", "AI/Missing.json");

        auto prefab = CreatePrefabDocument("InvalidTreeLevel", entities, container);
        ASSERT_TRUE(prefab.has_value());
        ASSERT_TRUE(context.AddPrefab(AZStd::move(*prefab)));

        BehaviorTree::BehaviorTreePrefabProcessor processor;
        processor.m_treeAssetIdLookup = [](const AZStd::string&)
        {
            return AZ::Data::AssetId{};
        };
        processor.Process(context);

        EXPECT_TRUE(context.GetRegisteredProductAssetDependencies().empty());
        EXPECT_TRUE(context.HasCompletedSuccessfully());
    }

    TEST_F(BehaviorTreePrefabProcessorTests, ReflectsQualifiedPrefabProcessorTypeName)
    {
        AZ::SerializeContext serializeContext;
        BehaviorTree::BehaviorTreePrefabProcessor::Reflect(&serializeContext);

        const auto classIds =
            serializeContext.FindClassId(AZ::Crc32("BehaviorTree::BehaviorTreePrefabProcessor"));
        EXPECT_NE(
            std::find(
                classIds.begin(), classIds.end(),
                azrtti_typeid<BehaviorTree::BehaviorTreePrefabProcessor>()),
            classIds.end());
    }
} // namespace UnitTest

AZ_TOOLS_UNIT_TEST_HOOK(DEFAULT_UNIT_TEST_ENV);
