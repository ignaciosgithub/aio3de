/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#if !defined(Q_MOC_RUN)
#include <QWidget>

#include <AzCore/Component/EntityId.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Math/Quaternion.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <IEditor.h>
#endif

class QDoubleSpinBox;
class QLabel;
class QTimer;

namespace AtomToolsFramework
{
    class RenderViewportWidget;
}

namespace SandboxEditor
{
    class EditorModularViewportCameraComposer;
}

//! A dockable secondary viewport that always renders through a free editor camera,
//! independent of the main viewport. During game mode the main viewport shows the
//! game camera while this pane keeps showing the scene from the editor camera,
//! similar to having Unity's Scene view and Game view open at the same time.
//! While in game mode the pane also allows selecting runtime entities and moving,
//! rotating or scaling them with a gizmo (or the transform bar at the bottom).
//! These edits apply to the running game only and are discarded on exiting game mode.
class SceneViewWidget
    : public QWidget
    , public IEditorNotifyListener
    , public AZ::TickBus::Handler
{
    Q_OBJECT
public:
    explicit SceneViewWidget(QWidget* parent = nullptr);
    ~SceneViewWidget() override;

    static void RegisterViewClass();

    // IEditorNotifyListener overrides ...
    void OnEditorNotifyEvent(EEditorNotifyEvent event) override;

    // AZ::TickBus overrides ...
    void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class GizmoMode
    {
        Translate,
        Rotate,
        Scale
    };

    void UpdateScene();
    void SetRenderingEnabled(bool enabled);
    void SyncCameraFromMainViewport();

    // Game mode interaction
    bool HandleGameModeMousePress(const QPoint& pos);
    bool HandleGameModeMouseMove(const QPoint& pos);
    bool HandleGameModeMouseRelease();
    bool HandleGameModeKeyPress(int key);
    void PickRuntimeEntity(const QPoint& pos);
    int PickGizmoAxis(const QPoint& pos) const;
    void DrawGameModeGizmo();
    void ClearRuntimeSelection();
    bool RuntimeSelectionValid() const;
    float GizmoAxisLength() const;
    AZ::Vector3 GizmoAxisDirection(int axis) const;
    bool ClosestAxisParamToMouseRay(const QPoint& pos, float& axisParam) const;

    // Transform bar
    QWidget* CreateTransformBar();
    void RefreshTransformBar();
    void ApplyTransformBar();

    AtomToolsFramework::RenderViewportWidget* m_renderViewport = nullptr;
    AZStd::unique_ptr<SandboxEditor::EditorModularViewportCameraComposer> m_cameraComposer;
    bool m_cameraSynced = false;

    // Game-mode selection/gizmo state
    AZ::EntityId m_runtimeSelection;
    GizmoMode m_gizmoMode = GizmoMode::Translate;
    int m_activeAxis = -1; // -1 none, 0 X, 1 Y, 2 Z
    bool m_dragging = false;
    QPoint m_lastMousePos;
    AZ::Vector3 m_dragStartPosition = AZ::Vector3::CreateZero();
    AZ::Quaternion m_dragStartRotation = AZ::Quaternion::CreateIdentity();
    float m_dragStartScale = 1.0f;
    float m_dragStartAxisParam = 0.0f;
    float m_dragAccumulated = 0.0f;

    // Transform bar widgets
    QWidget* m_transformBar = nullptr;
    QLabel* m_selectionLabel = nullptr;
    QDoubleSpinBox* m_posSpin[3] = { nullptr, nullptr, nullptr };
    QDoubleSpinBox* m_rotSpin[3] = { nullptr, nullptr, nullptr };
    QDoubleSpinBox* m_scaleSpin = nullptr;
    QTimer* m_transformBarTimer = nullptr;
    bool m_updatingTransformBar = false;
};
