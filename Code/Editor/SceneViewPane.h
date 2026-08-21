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

#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <IEditor.h>
#endif

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
class SceneViewWidget
    : public QWidget
    , public IEditorNotifyListener
{
    Q_OBJECT
public:
    explicit SceneViewWidget(QWidget* parent = nullptr);
    ~SceneViewWidget() override;

    static void RegisterViewClass();

    // IEditorNotifyListener overrides ...
    void OnEditorNotifyEvent(EEditorNotifyEvent event) override;

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void UpdateScene();
    void SetRenderingEnabled(bool enabled);
    void SyncCameraFromMainViewport();

    AtomToolsFramework::RenderViewportWidget* m_renderViewport = nullptr;
    AZStd::unique_ptr<SandboxEditor::EditorModularViewportCameraComposer> m_cameraComposer;
    bool m_cameraSynced = false;
};
