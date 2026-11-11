/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include "BehaviorTreeDefinition.h"

#include <AzCore/EBus/EBus.h>
#include <AzCore/Memory/SystemAllocator.h>

#include <QWidget>

class QCloseEvent;
class QFormLayout;
class QLabel;
class QListWidget;
class QTableWidget;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

namespace BehaviorTree
{
    class BehaviorTreeEditorRequests
        : public AZ::EBusTraits
    {
    public:
        static constexpr AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::Single;
        static constexpr AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Single;

        virtual void OpenTreeFile(const AZStd::string& path) = 0;
    };
    using BehaviorTreeEditorRequestBus = AZ::EBus<BehaviorTreeEditorRequests>;

    class BehaviorTreeEditorWindow
        : public QWidget
        , private BehaviorTreeEditorRequestBus::Handler
    {
        Q_OBJECT
    public:
        AZ_CLASS_ALLOCATOR(BehaviorTreeEditorWindow, AZ::SystemAllocator);

        explicit BehaviorTreeEditorWindow(QWidget* parent = nullptr);
        ~BehaviorTreeEditorWindow() override;

    private:
        void OpenTreeFile(const AZStd::string& path) override;
        void closeEvent(QCloseEvent* event) override;

        bool ConfirmDiscardOrSave();
        bool Save();
        bool SaveAs();
        bool WriteFile(const QString& path);
        bool ReadFile(const QString& path);
        void NewTree();
        void OpenTree();
        void Validate();
        void RebuildTree(const QString& selectedPath = {});
        void RebuildProperties();
        void RebuildBlackboard();
        void RebuildRuntimeBlackboard();
        void UpdateBlackboardFromTable();
        void AddTreeItem(QTreeWidgetItem* parent, const NodeDefinition& node, const QString& path);
        void ShowNodeMenu(const QPoint& position);
        void ShowAddChildMenu();
        void ShowWrapMenu();
        void AddChild(const AZStd::string& type);
        void WrapSelected(const AZStd::string& type);
        void DeleteSelected();
        void MoveSelected(int direction);
        void SelectPath(const QString& path);
        void OnSelectionChanged();
        void PollRuntimeDebug();
        void SetDirty(bool dirty);

        BehaviorTreeDefinition m_definition;
        AZStd::string m_filePath;
        QTreeWidget* m_treeWidget = nullptr;
        QWidget* m_propertyWidget = nullptr;
        QFormLayout* m_propertyLayout = nullptr;
        QTableWidget* m_blackboardTable = nullptr;
        QTableWidget* m_runtimeBlackboardTable = nullptr;
        QListWidget* m_validationList = nullptr;
        QLabel* m_statusLabel = nullptr;
        QTimer* m_debugTimer = nullptr;
        AZStd::vector<AZStd::string> m_activePaths;
        AZStd::string m_blackboardEditorError;
        bool m_dirty = false;
        bool m_rebuildingBlackboard = false;
    };
} // namespace BehaviorTree
