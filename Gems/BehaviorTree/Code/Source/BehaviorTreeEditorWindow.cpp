/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "BehaviorTreeEditorWindow.h"

#include "BehaviorTreeParser.h"
#include "BehaviorTreeWriter.h"
#include "NodeRegistry.h"

#include <AzCore/Component/TickBus.h>
#include <AzCore/Utils/Utils.h>
#include <AzToolsFramework/API/ToolsApplicationAPI.h>
#include <AzToolsFramework/Entity/EditorEntityContextBus.h>
#include <BehaviorTree/BehaviorTreeBus.h>

#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QCursor>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QAbstractItemView>
#include <QStyle>
#include <QStringList>

#include <AzCore/std/algorithm.h>
#include <AzCore/std/sort.h>

#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace BehaviorTree
{
    namespace
    {
        using IndexPath = AZStd::vector<size_t>;

        QString PathString(const IndexPath& path)
        {
            QStringList parts;
            for (size_t index : path)
            {
                parts.push_back(QString::number(aznumeric_cast<qulonglong>(index)));
            }
            return parts.join('/');
        }

        IndexPath ParsePath(const QString& pathString)
        {
            IndexPath path;
            const QStringList parts = pathString.split('/', Qt::SkipEmptyParts);
            for (const QString& part : parts)
            {
                bool converted = false;
                const qulonglong index = part.toULongLong(&converted);
                if (!converted)
                {
                    return {};
                }
                path.push_back(aznumeric_cast<size_t>(index));
            }
            return path;
        }

        NodeDefinition* GetNode(BehaviorTreeDefinition& definition, const IndexPath& path)
        {
            if (path.empty() || path.front() != 0)
            {
                return nullptr;
            }
            NodeDefinition* node = &definition.m_root;
            for (size_t pathIndex = 1; pathIndex < path.size(); ++pathIndex)
            {
                const size_t childIndex = path[pathIndex];
                if (childIndex >= node->m_children.size())
                {
                    return nullptr;
                }
                node = &node->m_children[childIndex];
            }
            return node;
        }

        NodeDefinition CreateNode(const NodeSchema& schema)
        {
            NodeDefinition node;
            node.m_type = schema.m_name;
            for (const NodeParameterSchema& parameter : schema.m_parameters)
            {
                node.m_parameters.emplace(parameter.m_name, parameter.m_defaultValue);
            }
            return node;
        }

        AZStd::string TypeName(BlackboardType type)
        {
            switch (type)
            {
            case BlackboardType::Bool:
                return "bool";
            case BlackboardType::Float:
                return "float";
            case BlackboardType::String:
                return "string";
            case BlackboardType::Vector3:
                return "vector3";
            case BlackboardType::EntityId:
                return "entity";
            }
            return "float";
        }

        bool ParseBlackboardType(const AZStd::string& text, BlackboardType& type)
        {
            if (text == "bool") type = BlackboardType::Bool;
            else if (text == "float") type = BlackboardType::Float;
            else if (text == "string") type = BlackboardType::String;
            else if (text == "vector3") type = BlackboardType::Vector3;
            else if (text == "entity") type = BlackboardType::EntityId;
            else return false;
            return true;
        }

        AZStd::string EntryText(const BlackboardEntry& entry)
        {
            if (!entry.m_hasExplicitValue)
            {
                return {};
            }
            switch (entry.m_type)
            {
            case BlackboardType::Bool:
                return AZStd::get<bool>(entry.m_value) ? "true" : "false";
            case BlackboardType::Float:
                return AZStd::string::format("%g", AZStd::get<float>(entry.m_value));
            case BlackboardType::String:
                return AZStd::get<AZStd::string>(entry.m_value);
            case BlackboardType::Vector3:
            {
                const AZ::Vector3& value = AZStd::get<AZ::Vector3>(entry.m_value);
                return AZStd::string::format("%g, %g, %g", value.GetX(), value.GetY(), value.GetZ());
            }
            case BlackboardType::EntityId:
                return AZStd::string::format("%llu", static_cast<AZ::u64>(AZStd::get<AZ::EntityId>(entry.m_value)));
            }
            return {};
        }

        bool ParseBlackboardEntryValue(const AZStd::string& text, BlackboardEntry& entry)
        {
            switch (entry.m_type)
            {
            case BlackboardType::Bool:
                if (text == "true")
                {
                    entry.m_value = true;
                    return true;
                }
                if (text == "false")
                {
                    entry.m_value = false;
                    return true;
                }
                return false;
            case BlackboardType::Float:
            {
                char* end = nullptr;
                const float value = strtof(text.c_str(), &end);
                if (end == text.c_str() || *end != '\0')
                {
                    return false;
                }
                entry.m_value = value;
                return true;
            }
            case BlackboardType::String:
                entry.m_value = text;
                return true;
            case BlackboardType::Vector3:
            {
                float x = 0.0f;
                float y = 0.0f;
                float z = 0.0f;
                char tail = '\0';
                if (sscanf(text.c_str(), " %f , %f , %f %c", &x, &y, &z, &tail) != 3)
                {
                    return false;
                }
                entry.m_value = AZ::Vector3(x, y, z);
                return true;
            }
            case BlackboardType::EntityId:
            {
                if (text.empty())
                {
                    entry.m_value = AZ::EntityId{};
                    return true;
                }
                char* end = nullptr;
                const AZ::u64 value = strtoull(text.c_str(), &end, 10);
                if (end == text.c_str() || *end != '\0')
                {
                    return false;
                }
                entry.m_value = AZ::EntityId(value);
                return true;
            }
            }
            return false;
        }

        QString NodeText(const NodeDefinition& node)
        {
            AZStd::string label = node.m_name;
            if (label.empty())
            {
                const ParameterValue* parameter = node.FindParameter("name");
                const AZStd::string* value = parameter ? AZStd::get_if<AZStd::string>(parameter) : nullptr;
                if (value)
                {
                    label = *value;
                }
            }
            return label.empty()
                ? QString::fromUtf8(node.m_type.c_str())
                : QString("%1: %2").arg(QString::fromUtf8(node.m_type.c_str()), QString::fromUtf8(label.c_str()));
        }

        QTreeWidgetItem* FindTreeItem(QTreeWidgetItem* item, const QString& path)
        {
            if (!item)
            {
                return nullptr;
            }
            if (item->data(0, Qt::UserRole).toString() == path)
            {
                return item;
            }
            for (int index = 0; index < item->childCount(); ++index)
            {
                if (QTreeWidgetItem* found = FindTreeItem(item->child(index), path))
                {
                    return found;
                }
            }
            return nullptr;
        }
    } // namespace

    BehaviorTreeEditorWindow::BehaviorTreeEditorWindow(QWidget* parent)
        : QWidget(parent)
    {
        setWindowTitle("Behavior Tree Editor");
        setMinimumSize(900, 600);

        auto* mainLayout = new QVBoxLayout(this);
        auto* fileToolbar = new QHBoxLayout();
        auto addFileButton = [this, fileToolbar](const QString& text, auto callback)
        {
            auto* button = new QPushButton(text, this);
            fileToolbar->addWidget(button);
            connect(button, &QPushButton::clicked, this, callback);
        };
        addFileButton("New", [this]() { NewTree(); });
        addFileButton("Open", [this]() { OpenTree(); });
        addFileButton("Save", [this]() { Save(); });
        addFileButton("Save As", [this]() { SaveAs(); });
        addFileButton("Validate", [this]() { Validate(); });
        fileToolbar->addStretch();
        mainLayout->addLayout(fileToolbar);

        auto* nodeToolbar = new QHBoxLayout();
        auto* addChildButton = new QPushButton("Add child", this);
        auto* wrapButton = new QPushButton("Wrap in decorator", this);
        auto* deleteButton = new QPushButton("Delete", this);
        auto* upButton = new QPushButton("Move up", this);
        auto* downButton = new QPushButton("Move down", this);
        nodeToolbar->addWidget(addChildButton);
        nodeToolbar->addWidget(wrapButton);
        nodeToolbar->addWidget(deleteButton);
        nodeToolbar->addWidget(upButton);
        nodeToolbar->addWidget(downButton);
        nodeToolbar->addStretch();
        mainLayout->addLayout(nodeToolbar);

        auto* splitter = new QSplitter(Qt::Horizontal, this);
        m_treeWidget = new QTreeWidget(splitter);
        m_treeWidget->setHeaderHidden(true);
        m_treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);

        auto* rightPanel = new QWidget(splitter);
        auto* rightLayout = new QVBoxLayout(rightPanel);
        rightLayout->addWidget(new QLabel("Node properties", rightPanel));
        m_propertyWidget = new QWidget(rightPanel);
        m_propertyLayout = new QFormLayout(m_propertyWidget);
        rightLayout->addWidget(m_propertyWidget);
        rightLayout->addWidget(new QLabel("Blackboard keys", rightPanel));
        m_blackboardTable = new QTableWidget(0, 3, rightPanel);
        m_blackboardTable->setHorizontalHeaderLabels({ "Key", "Type", "Default" });
        m_blackboardTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        rightLayout->addWidget(m_blackboardTable);
        auto* boardButtons = new QHBoxLayout();
        auto* addKeyButton = new QPushButton("Add key", rightPanel);
        auto* removeKeyButton = new QPushButton("Remove key", rightPanel);
        boardButtons->addWidget(addKeyButton);
        boardButtons->addWidget(removeKeyButton);
        rightLayout->addLayout(boardButtons);
        rightLayout->addWidget(new QLabel("Runtime blackboard (game mode)", rightPanel));
        m_runtimeBlackboardTable = new QTableWidget(0, 2, rightPanel);
        m_runtimeBlackboardTable->setHorizontalHeaderLabels({ "Key", "Value" });
        m_runtimeBlackboardTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_runtimeBlackboardTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        rightLayout->addWidget(m_runtimeBlackboardTable);
        splitter->addWidget(m_treeWidget);
        splitter->addWidget(rightPanel);
        splitter->setStretchFactor(0, 1);
        splitter->setStretchFactor(1, 2);
        mainLayout->addWidget(splitter, 1);

        m_validationList = new QListWidget(this);
        m_validationList->setMaximumHeight(120);
        mainLayout->addWidget(m_validationList);
        m_statusLabel = new QLabel(this);
        mainLayout->addWidget(m_statusLabel);

        connect(m_treeWidget, &QTreeWidget::itemSelectionChanged, this, [this]() { OnSelectionChanged(); });
        connect(m_treeWidget, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& point)
        {
            ShowNodeMenu(point);
        });
        connect(m_blackboardTable, &QTableWidget::itemChanged, this, [this]()
        {
            if (!m_rebuildingBlackboard)
            {
                UpdateBlackboardFromTable();
                SetDirty(true);
            }
        });
        connect(addKeyButton, &QPushButton::clicked, this, [this]()
        {
            bool accepted = false;
            const QString key = QInputDialog::getText(this, "Add blackboard key", "Key name:", QLineEdit::Normal, {}, &accepted);
            if (accepted && !key.trimmed().isEmpty())
            {
                BlackboardEntry entry;
                entry.m_type = BlackboardType::Float;
                entry.m_value = 0.0f;
                entry.m_hasExplicitValue = true;
                m_definition.m_blackboard.insert_or_assign(key.toUtf8().constData(), entry);
                RebuildBlackboard();
                SetDirty(true);
            }
        });
        connect(removeKeyButton, &QPushButton::clicked, this, [this]()
        {
            const int row = m_blackboardTable->currentRow();
            if (row >= 0)
            {
                const QString key = m_blackboardTable->item(row, 0)->text();
                m_definition.m_blackboard.erase(key.toUtf8().constData());
                RebuildBlackboard();
                SetDirty(true);
            }
        });
        connect(addChildButton, &QPushButton::clicked, this, [this]() { ShowAddChildMenu(); });
        connect(wrapButton, &QPushButton::clicked, this, [this]() { ShowWrapMenu(); });
        connect(deleteButton, &QPushButton::clicked, this, [this]() { DeleteSelected(); });
        connect(upButton, &QPushButton::clicked, this, [this]() { MoveSelected(-1); });
        connect(downButton, &QPushButton::clicked, this, [this]() { MoveSelected(1); });
        connect(m_validationList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item)
        {
            const QString message = item->text();
            const int rootIndex = message.indexOf("root");
            const int colonIndex = message.indexOf(':', rootIndex);
            if (rootIndex >= 0 && colonIndex > rootIndex)
            {
                QString path = message.mid(rootIndex, colonIndex - rootIndex);
                path.replace("/children[", "/");
                path.remove(']');
                path.replace("root", "0");
                SelectPath(path);
            }
        });

        m_debugTimer = new QTimer(this);
        m_debugTimer->setInterval(100);
        connect(m_debugTimer, &QTimer::timeout, this, [this]() { PollRuntimeDebug(); });
        m_debugTimer->start();

        NewTree();
        BehaviorTreeEditorRequestBus::Handler::BusConnect();
    }

    BehaviorTreeEditorWindow::~BehaviorTreeEditorWindow()
    {
        BehaviorTreeEditorRequestBus::Handler::BusDisconnect();
    }

    void BehaviorTreeEditorWindow::OpenTreeFile(const AZStd::string& path)
    {
        QString filePath = QString::fromUtf8(path.c_str());
        if (!QFileInfo(filePath).isAbsolute())
        {
            filePath = QString("%1/%2").arg(AZ::Utils::GetProjectPath().c_str(), filePath);
        }
        if (ConfirmDiscardOrSave())
        {
            ReadFile(filePath);
        }
    }

    void BehaviorTreeEditorWindow::closeEvent(QCloseEvent* event)
    {
        if (ConfirmDiscardOrSave())
        {
            event->accept();
        }
        else
        {
            event->ignore();
        }
    }

    bool BehaviorTreeEditorWindow::ConfirmDiscardOrSave()
    {
        if (!m_dirty)
        {
            return true;
        }
        const QMessageBox::StandardButton answer = QMessageBox::warning(
            this, "Unsaved Behavior Tree", "Save changes before continuing?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (answer == QMessageBox::Save)
        {
            return Save();
        }
        return answer == QMessageBox::Discard;
    }

    bool BehaviorTreeEditorWindow::Save()
    {
        if (m_filePath.empty())
        {
            return SaveAs();
        }
        return WriteFile(QString::fromUtf8(m_filePath.c_str()));
    }

    bool BehaviorTreeEditorWindow::SaveAs()
    {
        QString path = QFileDialog::getSaveFileName(
            this, "Save Behavior Tree", AZ::Utils::GetProjectPath().c_str(),
            "Behavior Trees (*.behaviortree.json)");
        if (path.isEmpty())
        {
            return false;
        }
        if (!path.endsWith(".behaviortree.json", Qt::CaseInsensitive))
        {
            path += ".behaviortree.json";
        }
        return WriteFile(path);
    }

    bool BehaviorTreeEditorWindow::WriteFile(const QString& path)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            QMessageBox::critical(this, "Save failed", file.errorString());
            return false;
        }
        const AZStd::string json = BehaviorTreeWriter::Write(m_definition);
        if (file.write(json.c_str(), aznumeric_cast<qint64>(json.size())) < 0)
        {
            QMessageBox::critical(this, "Save failed", file.errorString());
            return false;
        }
        m_filePath = path.toUtf8().constData();
        SetDirty(false);
        Validate();
        return true;
    }

    bool BehaviorTreeEditorWindow::ReadFile(const QString& path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
        {
            QMessageBox::critical(this, "Open failed", file.errorString());
            return false;
        }
        const QByteArray contents = file.readAll();
        BehaviorTreeParser::ParseOutcome result = BehaviorTreeParser::Parse(
            AZStd::string_view(contents.constData(), aznumeric_cast<size_t>(contents.size())));
        if (!result.IsSuccess())
        {
            QMessageBox::critical(this, "Invalid Behavior Tree", QString::fromUtf8(result.GetError().c_str()));
            return false;
        }
        m_definition = result.TakeValue();
        m_filePath = path.toUtf8().constData();
        m_blackboardEditorError.clear();
        SetDirty(false);
        RebuildTree();
        Validate();
        return true;
    }

    void BehaviorTreeEditorWindow::NewTree()
    {
        if (!ConfirmDiscardOrSave())
        {
            return;
        }
        m_definition = {};
        if (const NodeSchema* schema = NodeRegistry::Find("Wait"))
        {
            m_definition.m_root = CreateNode(*schema);
        }
        m_filePath.clear();
        m_blackboardEditorError.clear();
        SetDirty(true);
        RebuildTree("0");
        Validate();
    }

    void BehaviorTreeEditorWindow::OpenTree()
    {
        if (!ConfirmDiscardOrSave())
        {
            return;
        }
        const QString path = QFileDialog::getOpenFileName(
            this, "Open Behavior Tree", AZ::Utils::GetProjectPath().c_str(),
            "Behavior Trees (*.behaviortree.json)");
        if (!path.isEmpty())
        {
            ReadFile(path);
        }
    }

    void BehaviorTreeEditorWindow::Validate()
    {
        m_validationList->clear();
        if (!m_blackboardEditorError.empty())
        {
            m_validationList->addItem(QString::fromUtf8(m_blackboardEditorError.c_str()));
            m_statusLabel->setText("Validation failed");
            return;
        }
        const AZStd::string serialized = BehaviorTreeWriter::Write(m_definition);
        const BehaviorTreeParser::ParseOutcome result = BehaviorTreeParser::Parse(serialized);
        if (result.IsSuccess())
        {
            m_validationList->addItem("Valid behavior tree");
            m_statusLabel->setText("Validation succeeded");
        }
        else
        {
            m_validationList->addItem(QString::fromUtf8(result.GetError().c_str()));
            m_statusLabel->setText("Validation failed");
        }
    }

    void BehaviorTreeEditorWindow::RebuildTree(const QString& selectedPath)
    {
        const QString oldSelection = selectedPath.isEmpty() && m_treeWidget->currentItem()
            ? m_treeWidget->currentItem()->data(0, Qt::UserRole).toString()
            : selectedPath;
        const QSignalBlocker signalBlocker(m_treeWidget);
        m_treeWidget->clear();
        IndexPath rootPath{ 0 };
        AddTreeItem(nullptr, m_definition.m_root, PathString(rootPath));
        m_treeWidget->expandAll();
        if (!oldSelection.isEmpty())
        {
            SelectPath(oldSelection);
        }
        RebuildProperties();
        RebuildBlackboard();
        RebuildRuntimeBlackboard();
    }

    void BehaviorTreeEditorWindow::AddTreeItem(QTreeWidgetItem* parent, const NodeDefinition& node, const QString& path)
    {
        auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_treeWidget);
        item->setText(0, NodeText(node));
        item->setData(0, Qt::UserRole, path);
        const NodeSchema* schema = NodeRegistry::Find(node.m_type);
        if (schema)
        {
            QColor color;
            QStyle::StandardPixmap icon = QStyle::SP_FileIcon;
            if (schema->m_category == NodeCategory::Composite)
            {
                color = QColor(70, 130, 210);
                icon = QStyle::SP_ArrowRight;
            }
            else if (schema->m_category == NodeCategory::Decorator)
            {
                color = QColor(165, 95, 200);
                icon = QStyle::SP_ArrowUp;
            }
            else
            {
                color = QColor(70, 155, 100);
                icon = QStyle::SP_DialogApplyButton;
            }
            item->setForeground(0, QBrush(color));
            item->setIcon(0, QApplication::style()->standardIcon(icon));
        }
        for (size_t index = 0; index < node.m_children.size(); ++index)
        {
            IndexPath childPath = ParsePath(path);
            childPath.push_back(index);
            AddTreeItem(item, node.m_children[index], PathString(childPath));
        }
    }

    void BehaviorTreeEditorWindow::RebuildProperties()
    {
        while (QLayoutItem* item = m_propertyLayout->takeAt(0))
        {
            delete item->widget();
            delete item;
        }
        QTreeWidgetItem* selectedItem = m_treeWidget->currentItem();
        if (!selectedItem)
        {
            return;
        }
        const QString pathText = selectedItem->data(0, Qt::UserRole).toString();
        const IndexPath path = ParsePath(pathText);
        NodeDefinition* node = GetNode(m_definition, path);
        if (!node)
        {
            return;
        }
        const NodeSchema* schema = NodeRegistry::Find(node->m_type);
        if (!schema)
        {
            return;
        }

        const bool nameIsParameter = AZStd::any_of(
            schema->m_parameters.begin(), schema->m_parameters.end(),
            [](const NodeParameterSchema& parameter) { return parameter.m_name == "name"; });
        if (!nameIsParameter)
        {
            auto* nameEditor = new QLineEdit(QString::fromUtf8(node->m_name.c_str()), m_propertyWidget);
            m_propertyLayout->addRow("Name", nameEditor);
            connect(nameEditor, &QLineEdit::textChanged, this, [this, pathText](const QString& text)
            {
                if (NodeDefinition* selected = GetNode(m_definition, ParsePath(pathText)))
                {
                    selected->m_name = text.toUtf8().constData();
                    if (QTreeWidgetItem* item = m_treeWidget->currentItem())
                    {
                        item->setText(0, NodeText(*selected));
                    }
                    SetDirty(true);
                }
            });
        }

        for (const NodeParameterSchema& parameter : schema->m_parameters)
        {
            auto it = node->m_parameters.find(parameter.m_name);
            if (it == node->m_parameters.end())
            {
                continue;
            }
            const AZStd::string parameterName = parameter.m_name;
            const ParameterValue parameterValue = it->second;
            auto updateParameter = [this, pathText, parameterName](ParameterValue value)
            {
                if (NodeDefinition* selected = GetNode(m_definition, ParsePath(pathText)))
                {
                    selected->m_parameters.insert_or_assign(parameterName, AZStd::move(value));
                    if (QTreeWidgetItem* item = m_treeWidget->currentItem())
                    {
                        item->setText(0, NodeText(*selected));
                    }
                    SetDirty(true);
                }
            };

            if (parameter.m_kind == ParameterKind::Bool)
            {
                auto* editor = new QCheckBox(m_propertyWidget);
                editor->setChecked(AZStd::get<bool>(parameterValue));
                m_propertyLayout->addRow(QString::fromUtf8(parameterName.c_str()), editor);
                connect(editor, &QCheckBox::toggled, this, [updateParameter](bool value) { updateParameter(value); });
            }
            else if (parameter.m_kind == ParameterKind::Float)
            {
                auto* editor = new QDoubleSpinBox(m_propertyWidget);
                editor->setRange(-1000000000.0, 1000000000.0);
                editor->setDecimals(4);
                editor->setValue(AZStd::get<float>(parameterValue));
                m_propertyLayout->addRow(QString::fromUtf8(parameterName.c_str()), editor);
                connect(editor, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                    [updateParameter](double value) { updateParameter(static_cast<float>(value)); });
            }
            else if (parameter.m_kind == ParameterKind::Int)
            {
                auto* editor = new QSpinBox(m_propertyWidget);
                editor->setRange(-1000000, 1000000);
                editor->setValue(AZStd::get<AZ::s32>(parameterValue));
                m_propertyLayout->addRow(QString::fromUtf8(parameterName.c_str()), editor);
                connect(editor, qOverload<int>(&QSpinBox::valueChanged), this,
                    [updateParameter](int value) { updateParameter(static_cast<AZ::s32>(value)); });
            }
            else if (parameter.m_kind == ParameterKind::Enum)
            {
                auto* editor = new QComboBox(m_propertyWidget);
                for (const AZStd::string& value : parameter.m_enumValues)
                {
                    editor->addItem(QString::fromUtf8(value.c_str()));
                }
                editor->setCurrentText(QString::fromUtf8(AZStd::get<AZStd::string>(parameterValue).c_str()));
                m_propertyLayout->addRow(QString::fromUtf8(parameterName.c_str()), editor);
                connect(editor, &QComboBox::currentTextChanged, this,
                    [updateParameter](const QString& value) { updateParameter(AZStd::string(value.toUtf8().constData())); });
            }
            else if (parameter.m_kind == ParameterKind::Vector3)
            {
                const AZ::Vector3& value = AZStd::get<AZ::Vector3>(parameterValue);
                auto* editor = new QLineEdit(
                    QString("%1, %2, %3").arg(value.GetX()).arg(value.GetY()).arg(value.GetZ()), m_propertyWidget);
                m_propertyLayout->addRow(QString::fromUtf8(parameterName.c_str()), editor);
                connect(editor, &QLineEdit::editingFinished, this, [editor, updateParameter]()
                {
                    float x = 0.0f, y = 0.0f, z = 0.0f;
                    char tail = '\0';
                    const QByteArray bytes = editor->text().toUtf8();
                    if (sscanf(bytes.constData(), " %f , %f , %f %c", &x, &y, &z, &tail) == 3)
                    {
                        updateParameter(AZ::Vector3(x, y, z));
                    }
                });
            }
            else
            {
                auto* editor = new QLineEdit(
                    QString::fromUtf8(AZStd::get<AZStd::string>(parameterValue).c_str()), m_propertyWidget);
                m_propertyLayout->addRow(QString::fromUtf8(parameterName.c_str()), editor);
                connect(editor, &QLineEdit::textChanged, this,
                    [updateParameter](const QString& value) { updateParameter(AZStd::string(value.toUtf8().constData())); });
            }
        }
    }

    void BehaviorTreeEditorWindow::RebuildBlackboard()
    {
        m_rebuildingBlackboard = true;
        const QSignalBlocker blocker(m_blackboardTable);
        m_blackboardTable->setRowCount(0);
        AZStd::vector<AZStd::string> keys;
        keys.reserve(m_definition.m_blackboard.size());
        for (const auto& [key, entry] : m_definition.m_blackboard)
        {
            AZ_UNUSED(entry);
            keys.push_back(key);
        }
        AZStd::sort(keys.begin(), keys.end());
        for (const AZStd::string& key : keys)
        {
            const BlackboardEntry& entry = m_definition.m_blackboard.at(key);
            const int row = m_blackboardTable->rowCount();
            m_blackboardTable->insertRow(row);
            m_blackboardTable->setItem(row, 0, new QTableWidgetItem(QString::fromUtf8(key.c_str())));
            m_blackboardTable->setItem(row, 1, new QTableWidgetItem(QString::fromUtf8(TypeName(entry.m_type).c_str())));
            m_blackboardTable->setItem(row, 2, new QTableWidgetItem(QString::fromUtf8(EntryText(entry).c_str())));
        }
        m_rebuildingBlackboard = false;
    }

    void BehaviorTreeEditorWindow::RebuildRuntimeBlackboard()
    {
        m_runtimeBlackboardTable->setRowCount(0);
        AZStd::vector<AZStd::string> keys;
        keys.reserve(m_definition.m_blackboard.size());
        for (const auto& [key, entry] : m_definition.m_blackboard)
        {
            AZ_UNUSED(entry);
            keys.push_back(key);
        }
        AZStd::sort(keys.begin(), keys.end());
        for (const AZStd::string& key : keys)
        {
            const int row = m_runtimeBlackboardTable->rowCount();
            m_runtimeBlackboardTable->insertRow(row);
            m_runtimeBlackboardTable->setItem(row, 0, new QTableWidgetItem(QString::fromUtf8(key.c_str())));
            m_runtimeBlackboardTable->setItem(row, 1, new QTableWidgetItem("\u2014"));
        }
    }

    void BehaviorTreeEditorWindow::UpdateBlackboardFromTable()
    {
        AZStd::unordered_map<AZStd::string, BlackboardEntry> entries;
        m_blackboardEditorError.clear();
        for (int row = 0; row < m_blackboardTable->rowCount(); ++row)
        {
            const QString keyText = m_blackboardTable->item(row, 0)->text().trimmed();
            const QString typeText = m_blackboardTable->item(row, 1)->text().trimmed();
            const QString valueText = m_blackboardTable->item(row, 2)->text();
            BlackboardEntry entry;
            const AZStd::string key = keyText.toUtf8().constData();
            if (key.empty() || !ParseBlackboardType(typeText.toUtf8().constData(), entry.m_type))
            {
                m_blackboardEditorError = AZStd::string::format("blackboard row %d: invalid key or type", row + 1);
                continue;
            }
            if (valueText.isEmpty())
            {
                entry.m_hasExplicitValue = false;
                switch (entry.m_type)
                {
                case BlackboardType::Bool: entry.m_value = false; break;
                case BlackboardType::Float: entry.m_value = 0.0f; break;
                case BlackboardType::String: entry.m_value = AZStd::string{}; break;
                case BlackboardType::Vector3: entry.m_value = AZ::Vector3::CreateZero(); break;
                case BlackboardType::EntityId: entry.m_value = AZ::EntityId{}; break;
                }
            }
            else
            {
                entry.m_hasExplicitValue = true;
                if (!ParseBlackboardEntryValue(valueText.toUtf8().constData(), entry))
                {
                    m_blackboardEditorError = AZStd::string::format(
                        "blackboard.%s.value: invalid %s value", key.c_str(), TypeName(entry.m_type).c_str());
                    continue;
                }
            }
            entries.insert_or_assign(key, AZStd::move(entry));
        }
        m_definition.m_blackboard = AZStd::move(entries);
        RebuildRuntimeBlackboard();
        if (!m_blackboardEditorError.empty())
        {
            Validate();
        }
    }

    void BehaviorTreeEditorWindow::ShowNodeMenu(const QPoint& position)
    {
        QMenu menu(this);
        QAction* addAction = menu.addAction("Add child");
        QAction* wrapAction = menu.addAction("Wrap in decorator");
        menu.addSeparator();
        QAction* deleteAction = menu.addAction("Delete");
        QAction* upAction = menu.addAction("Move up");
        QAction* downAction = menu.addAction("Move down");
        QAction* selected = menu.exec(m_treeWidget->viewport()->mapToGlobal(position));
        if (selected == addAction) ShowAddChildMenu();
        else if (selected == wrapAction) ShowWrapMenu();
        else if (selected == deleteAction) DeleteSelected();
        else if (selected == upAction) MoveSelected(-1);
        else if (selected == downAction) MoveSelected(1);
    }

    void BehaviorTreeEditorWindow::ShowAddChildMenu()
    {
        QTreeWidgetItem* selectedItem = m_treeWidget->currentItem();
        NodeDefinition* selectedNode = selectedItem
            ? GetNode(m_definition, ParsePath(selectedItem->data(0, Qt::UserRole).toString()))
            : nullptr;
        if (!selectedNode)
        {
            return;
        }
        const NodeSchema* parentSchema = NodeRegistry::Find(selectedNode->m_type);
        if (!parentSchema || parentSchema->m_childCountRule == ChildCountRule::None ||
            (parentSchema->m_childCountRule == ChildCountRule::ExactlyOne && !selectedNode->m_children.empty()))
        {
            return;
        }

        QMenu menu(this);
        for (const NodeSchema& schema : NodeRegistry::GetAll())
        {
            QAction* action = menu.addAction(QString::fromUtf8(schema.m_name.c_str()));
            connect(action, &QAction::triggered, this, [this, type = schema.m_name]() { AddChild(type); });
        }
        menu.exec(QCursor::pos());
    }

    void BehaviorTreeEditorWindow::ShowWrapMenu()
    {
        if (!m_treeWidget->currentItem())
        {
            return;
        }
        QMenu menu(this);
        for (const NodeSchema& schema : NodeRegistry::GetAll())
        {
            if (schema.m_category == NodeCategory::Decorator)
            {
                QAction* action = menu.addAction(QString::fromUtf8(schema.m_name.c_str()));
                connect(action, &QAction::triggered, this, [this, type = schema.m_name]() { WrapSelected(type); });
            }
        }
        menu.exec(QCursor::pos());
    }

    void BehaviorTreeEditorWindow::AddChild(const AZStd::string& type)
    {
        QTreeWidgetItem* item = m_treeWidget->currentItem();
        if (!item)
        {
            return;
        }
        NodeDefinition* node = GetNode(m_definition, ParsePath(item->data(0, Qt::UserRole).toString()));
        const NodeSchema* parentSchema = node ? NodeRegistry::Find(node->m_type) : nullptr;
        const NodeSchema* childSchema = NodeRegistry::Find(type);
        if (!node || !parentSchema || !childSchema || parentSchema->m_childCountRule == ChildCountRule::None ||
            (parentSchema->m_childCountRule == ChildCountRule::ExactlyOne && !node->m_children.empty()))
        {
            return;
        }
        node->m_children.push_back(CreateNode(*childSchema));
        const QString path = item->data(0, Qt::UserRole).toString() +
            QString("/%1").arg(node->m_children.size() - 1);
        SetDirty(true);
        RebuildTree(path);
        Validate();
    }

    void BehaviorTreeEditorWindow::WrapSelected(const AZStd::string& type)
    {
        QTreeWidgetItem* item = m_treeWidget->currentItem();
        const NodeSchema* schema = NodeRegistry::Find(type);
        if (!item || !schema || schema->m_category != NodeCategory::Decorator)
        {
            return;
        }
        const IndexPath path = ParsePath(item->data(0, Qt::UserRole).toString());
        NodeDefinition wrapper = CreateNode(*schema);
        NodeDefinition* selected = GetNode(m_definition, path);
        if (!selected)
        {
            return;
        }
        if (path.size() == 1)
        {
            wrapper.m_children.push_back(AZStd::move(m_definition.m_root));
            m_definition.m_root = AZStd::move(wrapper);
        }
        else
        {
            IndexPath parentPath = path;
            const size_t childIndex = parentPath.back();
            parentPath.pop_back();
            NodeDefinition* parent = GetNode(m_definition, parentPath);
            if (!parent || childIndex >= parent->m_children.size())
            {
                return;
            }
            wrapper.m_children.push_back(AZStd::move(parent->m_children[childIndex]));
            parent->m_children[childIndex] = AZStd::move(wrapper);
        }
        SetDirty(true);
        RebuildTree(PathString(path));
        Validate();
    }

    void BehaviorTreeEditorWindow::DeleteSelected()
    {
        QTreeWidgetItem* item = m_treeWidget->currentItem();
        if (!item)
        {
            return;
        }
        IndexPath path = ParsePath(item->data(0, Qt::UserRole).toString());
        if (path.size() <= 1)
        {
            return;
        }
        const size_t childIndex = path.back();
        path.pop_back();
        NodeDefinition* parent = GetNode(m_definition, path);
        if (!parent || childIndex >= parent->m_children.size())
        {
            return;
        }
        parent->m_children.erase(parent->m_children.begin() + childIndex);
        SetDirty(true);
        RebuildTree(PathString(path));
        Validate();
    }

    void BehaviorTreeEditorWindow::MoveSelected(int direction)
    {
        QTreeWidgetItem* item = m_treeWidget->currentItem();
        if (!item)
        {
            return;
        }
        IndexPath path = ParsePath(item->data(0, Qt::UserRole).toString());
        if (path.size() <= 1)
        {
            return;
        }
        const size_t childIndex = path.back();
        path.pop_back();
        NodeDefinition* parent = GetNode(m_definition, path);
        if (!parent)
        {
            return;
        }
        const AZ::s64 targetIndex = aznumeric_cast<AZ::s64>(childIndex) + direction;
        if (targetIndex < 0 || targetIndex >= aznumeric_cast<AZ::s64>(parent->m_children.size()))
        {
            return;
        }
        AZStd::swap(parent->m_children[childIndex], parent->m_children[aznumeric_cast<size_t>(targetIndex)]);
        path.push_back(aznumeric_cast<size_t>(targetIndex));
        SetDirty(true);
        RebuildTree(PathString(path));
    }

    void BehaviorTreeEditorWindow::SelectPath(const QString& path)
    {
        for (int index = 0; index < m_treeWidget->topLevelItemCount(); ++index)
        {
            if (QTreeWidgetItem* item = FindTreeItem(m_treeWidget->topLevelItem(index), path))
            {
                m_treeWidget->setCurrentItem(item);
                return;
            }
        }
    }

    void BehaviorTreeEditorWindow::OnSelectionChanged()
    {
        RebuildProperties();
    }

    void BehaviorTreeEditorWindow::PollRuntimeDebug()
    {
        bool gameMode = false;
        AzToolsFramework::EditorEntityContextRequestBus::BroadcastResult(
            gameMode, &AzToolsFramework::EditorEntityContextRequests::IsEditorRunningGame);
        if (!gameMode)
        {
            m_activePaths.clear();
            RebuildRuntimeBlackboard();
        }
        else
        {
            AzToolsFramework::EntityIdList selectedEntities;
            AzToolsFramework::ToolsApplicationRequestBus::BroadcastResult(
                selectedEntities, &AzToolsFramework::ToolsApplicationRequests::GetSelectedEntities);
            AZ::EntityId runtimeEntityId;
            bool mapped = false;
            if (!selectedEntities.empty())
            {
                AzToolsFramework::EditorEntityContextRequestBus::BroadcastResult(
                    mapped, &AzToolsFramework::EditorEntityContextRequests::MapEditorIdToRuntimeId,
                    selectedEntities.front(), runtimeEntityId);
            }
            if (mapped && BehaviorTreeRequestBus::FindFirstHandler(runtimeEntityId))
            {
                BehaviorTreeRequestBus::EventResult(
                    m_activePaths, runtimeEntityId, &BehaviorTreeRequests::GetActiveNodePaths);
                for (int row = 0; row < m_runtimeBlackboardTable->rowCount(); ++row)
                {
                    const AZStd::string key = m_runtimeBlackboardTable->item(row, 0)->text().toUtf8().constData();
                    const auto entry = m_definition.m_blackboard.find(key);
                    if (entry == m_definition.m_blackboard.end())
                    {
                        continue;
                    }
                    QString text;
                    switch (entry->second.m_type)
                    {
                    case BlackboardType::Bool:
                    {
                        bool value = false;
                        BehaviorTreeRequestBus::EventResult(value, runtimeEntityId, &BehaviorTreeRequests::GetBlackboardBool, key);
                        text = value ? "true" : "false";
                        break;
                    }
                    case BlackboardType::Float:
                    {
                        float value = 0.0f;
                        BehaviorTreeRequestBus::EventResult(value, runtimeEntityId, &BehaviorTreeRequests::GetBlackboardFloat, key);
                        text = QString::number(value);
                        break;
                    }
                    case BlackboardType::String:
                    {
                        AZStd::string value;
                        BehaviorTreeRequestBus::EventResult(value, runtimeEntityId, &BehaviorTreeRequests::GetBlackboardString, key);
                        text = QString::fromUtf8(value.c_str());
                        break;
                    }
                    case BlackboardType::Vector3:
                    {
                        AZ::Vector3 value;
                        BehaviorTreeRequestBus::EventResult(value, runtimeEntityId, &BehaviorTreeRequests::GetBlackboardVector3, key);
                        text = QString("%1, %2, %3").arg(value.GetX()).arg(value.GetY()).arg(value.GetZ());
                        break;
                    }
                    case BlackboardType::EntityId:
                    {
                        AZ::EntityId value;
                        BehaviorTreeRequestBus::EventResult(value, runtimeEntityId, &BehaviorTreeRequests::GetBlackboardEntityId, key);
                        text = QString::fromUtf8(value.ToString().c_str());
                        break;
                    }
                    }
                    m_runtimeBlackboardTable->item(row, 1)->setText(text);
                }
            }
            else
            {
                m_activePaths.clear();
                RebuildRuntimeBlackboard();
            }
        }

        AZStd::unordered_set<AZStd::string> activePaths;
        for (const AZStd::string& activePath : m_activePaths)
        {
            const char* separator = strchr(activePath.c_str(), ' ');
            activePaths.emplace(activePath.c_str(), separator ? aznumeric_cast<size_t>(separator - activePath.c_str()) : activePath.size());
        }
        const QList<QTreeWidgetItem*> allItems = m_treeWidget->findItems("*", Qt::MatchWildcard | Qt::MatchRecursive);
        for (QTreeWidgetItem* item : allItems)
        {
            const AZStd::string path = item->data(0, Qt::UserRole).toString().toUtf8().constData();
            const bool running = activePaths.find(path) != activePaths.end();
            item->setBackground(0, running ? QBrush(QColor(255, 230, 130)) : QBrush());
        }
    }

    void BehaviorTreeEditorWindow::SetDirty(bool dirty)
    {
        m_dirty = dirty;
        const QString name = m_filePath.empty() ? "Untitled" : QFileInfo(QString::fromUtf8(m_filePath.c_str())).fileName();
        setWindowTitle(QString("%1%2 \u2014 Behavior Tree Editor").arg(m_dirty ? "*" : "").arg(name));
    }
} // namespace BehaviorTree
