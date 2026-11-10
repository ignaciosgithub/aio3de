/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <Prefab/PrefabUndoComponentPropertyTestFixture.h>

#include <AzCore/Component/TransformBus.h>
#include <AzToolsFramework/Entity/EditorEntityHelpers.h>
#include <AzToolsFramework/HotReload/GameCodeHotReloadSystemComponent.h>
#include <AzToolsFramework/Prefab/Undo/PrefabUndo.h>
#include <AzToolsFramework/Prefab/Undo/PrefabUndoComponentPropertyOverride.h>

namespace UnitTest
{
    using GameCodeHotReloadUndoTests = PrefabUndoComponentPropertyTestFixture;

    TEST_F(GameCodeHotReloadUndoTests, RebuildKeepsEntityPropertyUndoAndRedo)
    {
        const AZ::Vector3 originalTranslation(0.0f, 0.0f, 0.0f);
        const AZ::Vector3 editedTranslation(10.0f, 20.0f, 30.0f);
        const AZ::EntityId entityId = CreateEditorEntityUnderRoot("HotReloadUndoEntity");
        AZ::Entity* entity = AzToolsFramework::GetEntityById(entityId);
        ASSERT_NE(entity, nullptr);

        PrefabDom beforeState;
        m_instanceToTemplateInterface->GenerateEntityDomBySerializing(beforeState, *entity);

        AZ::TransformBus::Event(entityId, &AZ::TransformInterface::SetLocalTranslation, editedTranslation);
        entity = AzToolsFramework::GetEntityById(entityId);
        ASSERT_NE(entity, nullptr);
        PrefabDom afterState;
        m_instanceToTemplateInterface->GenerateEntityDomBySerializing(afterState, *entity);

        auto* undoNode = aznew PrefabUndoEntityUpdate("Edit transform translation");
        undoNode->Capture(beforeState, afterState, entityId);
        undoNode->Redo();
        m_undoStack->Post(undoNode);
        PropagateAllTemplateChanges();

        const TemplateId templateId = m_prefabEditorEntityOwnershipInterface->GetRootPrefabTemplateId();
        ASSERT_TRUE(AzToolsFramework::RebuildLevelEntities([] {}));
        EXPECT_EQ(m_prefabEditorEntityOwnershipInterface->GetRootPrefabTemplateId(), templateId);

        AZ::Vector3 currentTranslation;
        AZ::TransformBus::EventResult(currentTranslation, entityId, &AZ::TransformInterface::GetLocalTranslation);
        EXPECT_EQ(currentTranslation, editedTranslation);

        Undo();
        AZ::TransformBus::EventResult(currentTranslation, entityId, &AZ::TransformInterface::GetLocalTranslation);
        EXPECT_EQ(currentTranslation, originalTranslation);

        Redo();
        AZ::TransformBus::EventResult(currentTranslation, entityId, &AZ::TransformInterface::GetLocalTranslation);
        EXPECT_EQ(currentTranslation, editedTranslation);
    }

    TEST_F(GameCodeHotReloadUndoTests, RebuildKeepsAddEntityUndoAndRedo)
    {
        const AZ::EntityId entityId = CreateEditorEntityUnderRoot("HotReloadAddedEntity");
        ASSERT_NE(AzToolsFramework::GetEntityById(entityId), nullptr);

        ASSERT_TRUE(AzToolsFramework::RebuildLevelEntities([] {}));
        ASSERT_NE(AzToolsFramework::GetEntityById(entityId), nullptr);

        Undo();
        EXPECT_EQ(AzToolsFramework::GetEntityById(entityId), nullptr);

        Redo();
        EXPECT_NE(AzToolsFramework::GetEntityById(entityId), nullptr);
    }

    TEST_F(GameCodeHotReloadUndoTests, RebuildKeepsNestedPrefabOverrideUndo)
    {
        InstanceInfo carInstanceInfo;
        EntityInfo wheelEntityInfo;
        CreateCarPrefabHierarchy(carInstanceInfo, wheelEntityInfo);

        const AZ::Vector3 originalTranslation(0.0f, 0.0f, 0.0f);
        const AZ::Vector3 editedTranslation(10.0f, 20.0f, 30.0f);
        AZ::TransformBus::Event(
            wheelEntityInfo.m_entityId, &AZ::TransformInterface::SetLocalTranslation, editedTranslation);

        PropertyChangePatch changePatch =
            MakeTransformTranslationPropertyChangePatch(wheelEntityInfo.m_entityId, editedTranslation);
        PrefabDom propertyDomValue;
        ConvertToPrefabDomValue(propertyDomValue, changePatch.m_propertyValue);

        const AZStd::string entityAliasPath =
            m_instanceToTemplateInterface->GenerateEntityAliasPath(wheelEntityInfo.m_entityId);
        const AZ::Dom::Path pathToProperty =
            AZ::Dom::Path(entityAliasPath) / changePatch.m_pathToPropertyFromOwningEntity;

        InstanceOptionalReference carInstance =
            m_instanceEntityMapperInterface->FindOwningInstance(carInstanceInfo.m_containerEntityId);
        ASSERT_TRUE(carInstance.has_value());
        Instance* carInstancePointer = &carInstance->get();
        const TemplateId carTemplateId = carInstance->get().GetTemplateId();

        auto* undoNode = aznew PrefabUndoComponentPropertyOverride("Edit nested transform translation");
        undoNode->CaptureAndRedo(carInstance->get(), pathToProperty, propertyDomValue);
        m_undoStack->Post(undoNode);
        PropagateAllTemplateChanges();

        ASSERT_TRUE(AzToolsFramework::RebuildLevelEntities([] {}));
        carInstance = m_instanceEntityMapperInterface->FindOwningInstance(carInstanceInfo.m_containerEntityId);
        ASSERT_TRUE(carInstance.has_value());
        EXPECT_EQ(&carInstance->get(), carInstancePointer);
        EXPECT_EQ(carInstance->get().GetTemplateId(), carTemplateId);

        AZ::Vector3 currentTranslation;
        AZ::TransformBus::EventResult(
            currentTranslation, wheelEntityInfo.m_entityId, &AZ::TransformInterface::GetLocalTranslation);
        EXPECT_EQ(currentTranslation, editedTranslation);

        Undo();
        PropagateAllTemplateChanges();
        AZ::TransformBus::EventResult(
            currentTranslation, wheelEntityInfo.m_entityId, &AZ::TransformInterface::GetLocalTranslation);
        EXPECT_EQ(currentTranslation, originalTranslation);

        Redo();
        PropagateAllTemplateChanges();
        AZ::TransformBus::EventResult(
            currentTranslation, wheelEntityInfo.m_entityId, &AZ::TransformInterface::GetLocalTranslation);
        EXPECT_EQ(currentTranslation, editedTranslation);
    }
} // namespace UnitTest
