/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "EditorDefs.h"

#include "SceneViewPane.h"

// Qt
#include <QVBoxLayout>

// AzFramework
#include <AzFramework/Scene/SceneSystemInterface.h>
#include <AzFramework/Viewport/ViewportControllerList.h>

// AtomToolsFramework
#include <AtomToolsFramework/Viewport/ModularViewportCameraControllerRequestBus.h>
#include <AtomToolsFramework/Viewport/RenderViewportWidget.h>

// Atom
#include <Atom/RPI.Public/RenderPipeline.h>
#include <Atom/RPI.Public/ViewportContext.h>
#include <Atom/RPI.Public/ViewportContextBus.h>

// Editor
#include "EditorModularViewportCameraComposer.h"
#include "LyViewPaneNames.h"
#include "QtViewPaneManager.h"

SceneViewWidget::SceneViewWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_renderViewport = new AtomToolsFramework::RenderViewportWidget(this);
    layout->addWidget(m_renderViewport);

    const AzFramework::ViewportId viewportId = m_renderViewport->GetId();
    m_cameraComposer = AZStd::make_unique<SandboxEditor::EditorModularViewportCameraComposer>(viewportId);
    m_renderViewport->GetControllerList()->Add(m_cameraComposer->CreateModularViewportCameraController());

    UpdateScene();

    GetIEditor()->RegisterNotifyListener(this);
}

SceneViewWidget::~SceneViewWidget()
{
    GetIEditor()->UnregisterNotifyListener(this);
}

void SceneViewWidget::RegisterViewClass()
{
    AzToolsFramework::ViewPaneOptions opts;
    opts.paneRect = QRect(50, 50, 960, 540);
    opts.showInMenu = true;
    opts.isDockable = true;
    AzToolsFramework::RegisterViewPane<SceneViewWidget>(LyViewPane::SceneView, LyViewPane::CategoryViewport, opts);
}

void SceneViewWidget::OnEditorNotifyEvent(EEditorNotifyEvent event)
{
    switch (event)
    {
    case eNotify_OnEndSceneOpen:
    case eNotify_OnEndNewScene:
    case eNotify_OnCloseScene:
        UpdateScene();
        break;
    default:
        break;
    }
}

void SceneViewWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    SetRenderingEnabled(GetIEditor()->IsLevelLoaded());
    if (!m_cameraSynced)
    {
        SyncCameraFromMainViewport();
    }
}

void SceneViewWidget::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    SetRenderingEnabled(false);
}

void SceneViewWidget::UpdateScene()
{
    if (auto* sceneSystem = AzFramework::SceneSystemInterface::Get())
    {
        if (AZStd::shared_ptr<AzFramework::Scene> mainScene = sceneSystem->GetScene(AzFramework::Scene::MainSceneName))
        {
            m_renderViewport->SetScene(mainScene);
        }
    }

    SetRenderingEnabled(isVisible() && GetIEditor()->IsLevelLoaded());
}

void SceneViewWidget::SetRenderingEnabled(bool enabled)
{
    auto viewportContext = m_renderViewport->GetViewportContext();
    if (!viewportContext)
    {
        return;
    }

    if (auto renderPipeline = viewportContext->GetCurrentPipeline())
    {
        if (enabled)
        {
            m_renderViewport->show();
            renderPipeline->AddToRenderTick();
        }
        else
        {
            renderPipeline->RemoveFromRenderTick();
        }
    }
}

void SceneViewWidget::SyncCameraFromMainViewport()
{
    auto viewportContextManager = AZ::Interface<AZ::RPI::ViewportContextRequestsInterface>::Get();
    if (!viewportContextManager)
    {
        return;
    }

    auto defaultViewportContext = viewportContextManager->GetDefaultViewportContext();
    if (!defaultViewportContext)
    {
        return;
    }

    const AZ::Transform cameraTransform = defaultViewportContext->GetCameraTransform();
    AtomToolsFramework::ModularViewportCameraControllerRequestBus::Event(
        m_renderViewport->GetId(),
        &AtomToolsFramework::ModularViewportCameraControllerRequestBus::Events::InterpolateToTransform,
        cameraTransform,
        0.0f);

    m_cameraSynced = true;
}
