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
#include <QDoubleSpinBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QTimer>
#include <QVBoxLayout>

// AzCore
#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Interface/Interface.h>
#include <AzCore/Math/Aabb.h>

// AzFramework
#include <AzFramework/Entity/GameEntityContextBus.h>
#include <AzFramework/Render/IntersectorInterface.h>
#include <AzFramework/Scene/SceneSystemInterface.h>
#include <AzFramework/Viewport/ViewportControllerList.h>
#include <AzFramework/Visibility/EntityBoundsUnionBus.h>

// AzToolsFramework
#include <AzToolsFramework/Viewport/ViewportTypes.h>

// AtomToolsFramework
#include <AtomToolsFramework/Viewport/ModularViewportCameraControllerRequestBus.h>
#include <AtomToolsFramework/Viewport/RenderViewportWidget.h>

// Atom
#include <Atom/RPI.Public/AuxGeom/AuxGeomDraw.h>
#include <Atom/RPI.Public/AuxGeom/AuxGeomFeatureProcessorInterface.h>
#include <Atom/RPI.Public/RenderPipeline.h>
#include <Atom/RPI.Public/Scene.h>
#include <Atom/RPI.Public/ViewportContext.h>
#include <Atom/RPI.Public/ViewportContextBus.h>

// Editor
#include "EditorModularViewportCameraComposer.h"
#include "LyViewPaneNames.h"
#include "QtViewPaneManager.h"

namespace
{
    constexpr float GizmoPickPixelThreshold = 12.0f;
    constexpr float RotateDegreesPerPixel = 0.5f;
    constexpr float ScalePerPixel = 0.01f;

    const AZ::Color GizmoAxisColors[3] = {
        AZ::Color(1.0f, 0.2f, 0.2f, 1.0f),
        AZ::Color(0.2f, 1.0f, 0.2f, 1.0f),
        AZ::Color(0.3f, 0.4f, 1.0f, 1.0f),
    };
    const AZ::Color GizmoActiveColor(1.0f, 1.0f, 0.2f, 1.0f);
    const AZ::Color GizmoSelectionBoxColor(1.0f, 0.6f, 0.1f, 1.0f);

    AzFramework::EntityContextId GameEntityContextId()
    {
        AzFramework::EntityContextId contextId = AzFramework::EntityContextId::CreateNull();
        AzFramework::GameEntityContextRequestBus::BroadcastResult(
            contextId, &AzFramework::GameEntityContextRequestBus::Events::GetGameEntityContextId);
        return contextId;
    }

    AZ::RPI::AuxGeomDrawPtr GetAuxGeomDrawQueue(AtomToolsFramework::RenderViewportWidget* renderViewport)
    {
        if (!renderViewport)
        {
            return nullptr;
        }

        auto viewportContext = renderViewport->GetViewportContext();
        if (!viewportContext)
        {
            return nullptr;
        }

        AZ::RPI::ScenePtr scene = viewportContext->GetRenderScene();
        if (!scene)
        {
            return nullptr;
        }

        auto auxGeomFP = scene->GetFeatureProcessor<AZ::RPI::AuxGeomFeatureProcessorInterface>();
        if (!auxGeomFP)
        {
            return nullptr;
        }

        return auxGeomFP->GetOrCreateDrawQueueForView(renderViewport->GetDefaultCamera().get());
    }

    AZ::Aabb SelectedEntityWorldBounds(AZ::EntityId entityId)
    {
        AZ::Aabb bounds = AZ::Aabb::CreateNull();
        if (auto* boundsUnion = AZ::Interface<AzFramework::IEntityBoundsUnion>::Get())
        {
            bounds = boundsUnion->GetEntityWorldBoundsUnion(entityId);
        }

        if (!bounds.IsValid())
        {
            AZ::Vector3 position = AZ::Vector3::CreateZero();
            AZ::TransformBus::EventResult(position, entityId, &AZ::TransformBus::Events::GetWorldTranslation);
            bounds = AZ::Aabb::CreateCenterRadius(position, 0.5f);
        }

        return bounds;
    }

    float PointToSegmentDistance2d(const QPointF& point, const QPointF& segmentStart, const QPointF& segmentEnd)
    {
        const QPointF segment = segmentEnd - segmentStart;
        const float segmentLengthSq = aznumeric_cast<float>(QPointF::dotProduct(segment, segment));
        float t = 0.0f;
        if (segmentLengthSq > 0.0f)
        {
            t = AZ::GetClamp(
                aznumeric_cast<float>(QPointF::dotProduct(point - segmentStart, segment)) / segmentLengthSq, 0.0f, 1.0f);
        }
        const QPointF closest = segmentStart + t * segment;
        const QPointF delta = point - closest;
        return aznumeric_cast<float>(AZStd::sqrt(QPointF::dotProduct(delta, delta)));
    }
} // namespace

SceneViewWidget::SceneViewWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_renderViewport = new AtomToolsFramework::RenderViewportWidget(this);
    layout->addWidget(m_renderViewport, 1);

    layout->addWidget(CreateTransformBar());

    const AzFramework::ViewportId viewportId = m_renderViewport->GetId();
    m_cameraComposer = AZStd::make_unique<SandboxEditor::EditorModularViewportCameraComposer>(viewportId);
    m_renderViewport->GetControllerList()->Add(m_cameraComposer->CreateModularViewportCameraController());

    m_renderViewport->installEventFilter(this);

    UpdateScene();

    GetIEditor()->RegisterNotifyListener(this);

    if (GetIEditor()->IsInGameMode())
    {
        AZ::TickBus::Handler::BusConnect();
    }
}

SceneViewWidget::~SceneViewWidget()
{
    AZ::TickBus::Handler::BusDisconnect();
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
    case eNotify_OnBeginGameMode:
        if (!AZ::TickBus::Handler::BusIsConnected())
        {
            AZ::TickBus::Handler::BusConnect();
        }
        break;
    case eNotify_OnEndGameMode:
        AZ::TickBus::Handler::BusDisconnect();
        ClearRuntimeSelection();
        break;
    default:
        break;
    }
}

void SceneViewWidget::OnTick([[maybe_unused]] float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
{
    if (!GetIEditor()->IsInGameMode())
    {
        return;
    }

    if (m_runtimeSelection.IsValid() && !RuntimeSelectionValid())
    {
        // The selected runtime entity was despawned by the game.
        ClearRuntimeSelection();
    }

    if (isVisible() && RuntimeSelectionValid())
    {
        DrawGameModeGizmo();
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

bool SceneViewWidget::event(QEvent* event)
{
    switch (event->type())
    {
    // While the pane is docked/undocked Qt destroys and recreates the underlying
    // native window. Pause rendering across the reparent so no frame is presented
    // to a surface that is about to be (or was just) destroyed.
    case QEvent::ParentAboutToChange:
        SetRenderingEnabled(false);
        break;
    case QEvent::ParentChange:
        SetRenderingEnabled(isVisible() && GetIEditor()->IsLevelLoaded());
        break;
    default:
        break;
    }
    return QWidget::event(event);
}

bool SceneViewWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_renderViewport && GetIEditor()->IsInGameMode())
    {
        switch (event->type())
        {
        case QEvent::MouseButtonPress:
        {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                if (HandleGameModeMousePress(mouseEvent->pos()))
                {
                    return true;
                }
            }
            break;
        }
        case QEvent::MouseMove:
        {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (HandleGameModeMouseMove(mouseEvent->pos()))
            {
                return true;
            }
            break;
        }
        case QEvent::MouseButtonRelease:
        {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                if (HandleGameModeMouseRelease())
                {
                    return true;
                }
            }
            break;
        }
        case QEvent::KeyPress:
        {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (HandleGameModeKeyPress(keyEvent->key()))
            {
                return true;
            }
            break;
        }
        default:
            break;
        }
    }

    return QWidget::eventFilter(watched, event);
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

bool SceneViewWidget::HandleGameModeMousePress(const QPoint& pos)
{
    m_lastMousePos = pos;

    if (RuntimeSelectionValid())
    {
        const int axis = PickGizmoAxis(pos);
        if (axis >= 0)
        {
            m_activeAxis = axis;
            m_dragging = true;
            m_dragAccumulated = 0.0f;

            AZ::TransformBus::EventResult(
                m_dragStartPosition, m_runtimeSelection, &AZ::TransformBus::Events::GetWorldTranslation);
            AZ::TransformBus::EventResult(
                m_dragStartRotation, m_runtimeSelection, &AZ::TransformBus::Events::GetWorldRotationQuaternion);
            AZ::TransformBus::EventResult(
                m_dragStartScale, m_runtimeSelection, &AZ::TransformBus::Events::GetLocalUniformScale);

            if (m_gizmoMode == GizmoMode::Translate)
            {
                if (!ClosestAxisParamToMouseRay(pos, m_dragStartAxisParam))
                {
                    m_dragStartAxisParam = 0.0f;
                }
            }
            return true;
        }
    }

    PickRuntimeEntity(pos);
    return m_runtimeSelection.IsValid();
}

bool SceneViewWidget::HandleGameModeMouseMove(const QPoint& pos)
{
    if (!m_dragging || !RuntimeSelectionValid())
    {
        m_lastMousePos = pos;
        return false;
    }

    const QPoint delta = pos - m_lastMousePos;
    m_lastMousePos = pos;

    switch (m_gizmoMode)
    {
    case GizmoMode::Translate:
    {
        float axisParam = 0.0f;
        if (ClosestAxisParamToMouseRay(pos, axisParam))
        {
            const AZ::Vector3 newPosition =
                m_dragStartPosition + GizmoAxisDirection(m_activeAxis) * (axisParam - m_dragStartAxisParam);
            AZ::TransformBus::Event(m_runtimeSelection, &AZ::TransformBus::Events::SetWorldTranslation, newPosition);
        }
        break;
    }
    case GizmoMode::Rotate:
    {
        m_dragAccumulated += aznumeric_cast<float>(delta.x()) * RotateDegreesPerPixel;
        const AZ::Quaternion rotation =
            AZ::Quaternion::CreateFromAxisAngle(GizmoAxisDirection(m_activeAxis), AZ::DegToRad(m_dragAccumulated)) *
            m_dragStartRotation;
        AZ::TransformBus::Event(
            m_runtimeSelection, &AZ::TransformBus::Events::SetWorldRotationQuaternion, rotation.GetNormalized());
        break;
    }
    case GizmoMode::Scale:
    {
        m_dragAccumulated += aznumeric_cast<float>(delta.x()) * ScalePerPixel;
        const float scale = AZ::GetMax(0.01f, m_dragStartScale * (1.0f + m_dragAccumulated));
        AZ::TransformBus::Event(m_runtimeSelection, &AZ::TransformBus::Events::SetLocalUniformScale, scale);
        break;
    }
    }

    return true;
}

bool SceneViewWidget::HandleGameModeMouseRelease()
{
    if (m_dragging)
    {
        m_dragging = false;
        m_activeAxis = -1;
        return true;
    }
    return false;
}

bool SceneViewWidget::HandleGameModeKeyPress(int key)
{
    switch (key)
    {
    case Qt::Key_W:
        if (RuntimeSelectionValid())
        {
            m_gizmoMode = GizmoMode::Translate;
            return true;
        }
        break;
    case Qt::Key_E:
        if (RuntimeSelectionValid())
        {
            m_gizmoMode = GizmoMode::Rotate;
            return true;
        }
        break;
    case Qt::Key_R:
        if (RuntimeSelectionValid())
        {
            m_gizmoMode = GizmoMode::Scale;
            return true;
        }
        break;
    case Qt::Key_Escape:
        if (m_runtimeSelection.IsValid())
        {
            ClearRuntimeSelection();
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

void SceneViewWidget::PickRuntimeEntity(const QPoint& pos)
{
    const auto screenPoint =
        AzToolsFramework::ViewportInteraction::ScreenPointFromQPoint(pos * m_renderViewport->devicePixelRatioF());
    const auto ray = m_renderViewport->ViewportScreenToWorldRay(screenPoint);

    AzFramework::RenderGeometry::RayRequest request;
    request.m_startWorldPosition = ray.m_origin;
    request.m_endWorldPosition = ray.m_origin + ray.m_direction * 10000.0f;
    request.m_onlyVisible = true;

    AzFramework::RenderGeometry::RayResult result;
    AzFramework::RenderGeometry::IntersectorBus::EventResult(
        result, GameEntityContextId(), &AzFramework::RenderGeometry::IntersectorBus::Events::RayIntersect, request);

    const AZ::EntityId hitEntity = result ? result.m_entityAndComponent.GetEntityId() : AZ::EntityId();
    if (hitEntity.IsValid())
    {
        m_runtimeSelection = hitEntity;
        m_gizmoMode = GizmoMode::Translate;
    }
    else
    {
        ClearRuntimeSelection();
    }

    RefreshTransformBar();
}

int SceneViewWidget::PickGizmoAxis(const QPoint& pos) const
{
    AZ::Vector3 origin = AZ::Vector3::CreateZero();
    AZ::TransformBus::EventResult(origin, m_runtimeSelection, &AZ::TransformBus::Events::GetWorldTranslation);

    const float axisLength = GizmoAxisLength();
    const qreal pixelRatio = m_renderViewport->devicePixelRatioF();
    const QPointF mouse = QPointF(pos) * pixelRatio;

    int closestAxis = -1;
    float closestDistance = GizmoPickPixelThreshold * aznumeric_cast<float>(pixelRatio);

    for (int axis = 0; axis < 3; ++axis)
    {
        const AZ::Vector3 tip = origin + GizmoAxisDirection(axis) * axisLength;
        const auto originScreen = m_renderViewport->ViewportWorldToScreen(origin);
        const auto tipScreen = m_renderViewport->ViewportWorldToScreen(tip);

        const QPointF originPoint(originScreen.m_x, originScreen.m_y);
        const QPointF tipPoint(tipScreen.m_x, tipScreen.m_y);

        const float distance = PointToSegmentDistance2d(mouse, originPoint, tipPoint);
        if (distance < closestDistance)
        {
            closestDistance = distance;
            closestAxis = axis;
        }
    }

    return closestAxis;
}

void SceneViewWidget::DrawGameModeGizmo()
{
    AZ::RPI::AuxGeomDrawPtr auxGeom = GetAuxGeomDrawQueue(m_renderViewport);
    if (!auxGeom)
    {
        return;
    }

    AZ::Vector3 origin = AZ::Vector3::CreateZero();
    AZ::TransformBus::EventResult(origin, m_runtimeSelection, &AZ::TransformBus::Events::GetWorldTranslation);

    // Selection highlight
    const AZ::Aabb bounds = SelectedEntityWorldBounds(m_runtimeSelection);
    auxGeom->DrawAabb(
        bounds, GizmoSelectionBoxColor, AZ::RPI::AuxGeomDraw::DrawStyle::Line, AZ::RPI::AuxGeomDraw::DepthTest::Off,
        AZ::RPI::AuxGeomDraw::DepthWrite::Off);

    const float axisLength = GizmoAxisLength();

    for (int axis = 0; axis < 3; ++axis)
    {
        const AZ::Color color = (axis == m_activeAxis) ? GizmoActiveColor : GizmoAxisColors[axis];
        const AZ::Vector3 direction = GizmoAxisDirection(axis);

        if (m_gizmoMode == GizmoMode::Rotate)
        {
            // Rotation rings around each axis
            constexpr int segments = 32;
            AZ::Vector3 verts[segments];
            AZ::Vector3 orthogonal = direction.GetOrthogonalVector().GetNormalized();
            for (int i = 0; i < segments; ++i)
            {
                const float angle = AZ::Constants::TwoPi * aznumeric_cast<float>(i) / aznumeric_cast<float>(segments);
                const AZ::Quaternion ringRotation = AZ::Quaternion::CreateFromAxisAngle(direction, angle);
                verts[i] = origin + ringRotation.TransformVector(orthogonal) * axisLength;
            }

            AZ::RPI::AuxGeomDraw::AuxGeomDynamicDrawArguments drawArgs;
            drawArgs.m_verts = verts;
            drawArgs.m_vertCount = segments;
            drawArgs.m_colors = &color;
            drawArgs.m_colorCount = 1;
            drawArgs.m_depthTest = AZ::RPI::AuxGeomDraw::DepthTest::Off;
            drawArgs.m_depthWrite = AZ::RPI::AuxGeomDraw::DepthWrite::Off;
            auxGeom->DrawPolylines(drawArgs, AZ::RPI::AuxGeomDraw::PolylineEnd::Closed);
        }
        else
        {
            const AZ::Vector3 tip = origin + direction * axisLength;
            const AZ::Vector3 verts[2] = { origin, tip };

            AZ::RPI::AuxGeomDraw::AuxGeomDynamicDrawArguments drawArgs;
            drawArgs.m_verts = verts;
            drawArgs.m_vertCount = 2;
            drawArgs.m_colors = &color;
            drawArgs.m_colorCount = 1;
            drawArgs.m_depthTest = AZ::RPI::AuxGeomDraw::DepthTest::Off;
            drawArgs.m_depthWrite = AZ::RPI::AuxGeomDraw::DepthWrite::Off;
            auxGeom->DrawLines(drawArgs);

            // Handle tips: sphere for translate, box for scale
            if (m_gizmoMode == GizmoMode::Scale)
            {
                const float tipSize = axisLength * 0.05f;
                auxGeom->DrawAabb(
                    AZ::Aabb::CreateCenterRadius(tip, tipSize), color, AZ::RPI::AuxGeomDraw::DrawStyle::Solid,
                    AZ::RPI::AuxGeomDraw::DepthTest::Off, AZ::RPI::AuxGeomDraw::DepthWrite::Off);
            }
            else
            {
                auxGeom->DrawSphere(
                    tip, axisLength * 0.04f, color, AZ::RPI::AuxGeomDraw::DrawStyle::Solid,
                    AZ::RPI::AuxGeomDraw::DepthTest::Off, AZ::RPI::AuxGeomDraw::DepthWrite::Off);
            }
        }
    }
}

void SceneViewWidget::ClearRuntimeSelection()
{
    m_runtimeSelection = AZ::EntityId();
    m_dragging = false;
    m_activeAxis = -1;
    RefreshTransformBar();
}

bool SceneViewWidget::RuntimeSelectionValid() const
{
    if (!m_runtimeSelection.IsValid())
    {
        return false;
    }

    AZ::Entity* entity = nullptr;
    AZ::ComponentApplicationBus::BroadcastResult(
        entity, &AZ::ComponentApplicationBus::Events::FindEntity, m_runtimeSelection);
    return entity != nullptr && entity->GetState() == AZ::Entity::State::Active;
}

float SceneViewWidget::GizmoAxisLength() const
{
    // Size the gizmo at 2x the object's size so it is clearly visible outside
    // the object bounds, with a sensible minimum for tiny/point entities.
    const AZ::Aabb bounds = SelectedEntityWorldBounds(m_runtimeSelection);
    const AZ::Vector3 halfExtents = bounds.GetExtents() * 0.5f;
    const float maxHalfExtent = halfExtents.GetMaxElement();
    return AZ::GetMax(0.5f, maxHalfExtent) * 2.0f;
}

AZ::Vector3 SceneViewWidget::GizmoAxisDirection(int axis) const
{
    AZ::Quaternion rotation = AZ::Quaternion::CreateIdentity();
    AZ::TransformBus::EventResult(rotation, m_runtimeSelection, &AZ::TransformBus::Events::GetWorldRotationQuaternion);

    switch (axis)
    {
    case 0:
        return rotation.TransformVector(AZ::Vector3::CreateAxisX()).GetNormalized();
    case 1:
        return rotation.TransformVector(AZ::Vector3::CreateAxisY()).GetNormalized();
    default:
        return rotation.TransformVector(AZ::Vector3::CreateAxisZ()).GetNormalized();
    }
}

bool SceneViewWidget::ClosestAxisParamToMouseRay(const QPoint& pos, float& axisParam) const
{
    const auto screenPoint =
        AzToolsFramework::ViewportInteraction::ScreenPointFromQPoint(pos * m_renderViewport->devicePixelRatioF());
    const auto ray = m_renderViewport->ViewportScreenToWorldRay(screenPoint);

    const AZ::Vector3 axisOrigin = m_dragStartPosition;
    const AZ::Vector3 axisDirection = GizmoAxisDirection(m_activeAxis);

    const AZ::Vector3 rayDirection = ray.m_direction.GetNormalized();
    const float axisDotRay = axisDirection.Dot(rayDirection);
    const float denominator = 1.0f - axisDotRay * axisDotRay;
    if (denominator < 1e-4f)
    {
        // Axis is nearly parallel to the view ray; no reliable closest point.
        return false;
    }

    const AZ::Vector3 originDelta = ray.m_origin - axisOrigin;
    axisParam = (originDelta.Dot(axisDirection) - originDelta.Dot(rayDirection) * axisDotRay) / denominator;
    return true;
}

QWidget* SceneViewWidget::CreateTransformBar()
{
    m_transformBar = new QWidget(this);
    auto* barLayout = new QHBoxLayout(m_transformBar);
    barLayout->setContentsMargins(4, 2, 4, 2);
    barLayout->setSpacing(4);

    m_selectionLabel = new QLabel(tr("No entity selected"), m_transformBar);
    m_selectionLabel->setMinimumWidth(120);
    barLayout->addWidget(m_selectionLabel);

    const auto makeSpin = [this](double min, double max) -> QDoubleSpinBox*
    {
        auto* spin = new QDoubleSpinBox(m_transformBar);
        spin->setRange(min, max);
        spin->setDecimals(3);
        spin->setSingleStep(0.1);
        spin->setKeyboardTracking(false);
        connect(
            spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this]([[maybe_unused]] double value)
            {
                ApplyTransformBar();
            });
        return spin;
    };

    barLayout->addWidget(new QLabel(tr("Pos"), m_transformBar));
    for (int i = 0; i < 3; ++i)
    {
        m_posSpin[i] = makeSpin(-1.0e6, 1.0e6);
        barLayout->addWidget(m_posSpin[i]);
    }

    barLayout->addWidget(new QLabel(tr("Rot"), m_transformBar));
    for (int i = 0; i < 3; ++i)
    {
        m_rotSpin[i] = makeSpin(-360.0, 360.0);
        barLayout->addWidget(m_rotSpin[i]);
    }

    barLayout->addWidget(new QLabel(tr("Scale"), m_transformBar));
    m_scaleSpin = makeSpin(0.001, 1.0e4);
    barLayout->addWidget(m_scaleSpin);

    barLayout->addStretch();

    m_transformBarTimer = new QTimer(this);
    m_transformBarTimer->setInterval(100);
    connect(
        m_transformBarTimer, &QTimer::timeout, this,
        [this]
        {
            RefreshTransformBar();
        });
    m_transformBarTimer->start();

    m_transformBar->setVisible(false);
    return m_transformBar;
}

void SceneViewWidget::RefreshTransformBar()
{
    const bool showBar = GetIEditor()->IsInGameMode() && RuntimeSelectionValid();
    if (m_transformBar->isVisible() != showBar)
    {
        m_transformBar->setVisible(showBar);
    }

    if (!showBar)
    {
        return;
    }

    AZ::Entity* entity = nullptr;
    AZ::ComponentApplicationBus::BroadcastResult(
        entity, &AZ::ComponentApplicationBus::Events::FindEntity, m_runtimeSelection);
    if (entity)
    {
        m_selectionLabel->setText(QString::fromUtf8(entity->GetName().c_str()));
    }

    // Do not fight the user while they are typing in a field.
    const auto anySpinHasFocus = [this]
    {
        for (int i = 0; i < 3; ++i)
        {
            if (m_posSpin[i]->hasFocus() || m_rotSpin[i]->hasFocus())
            {
                return true;
            }
        }
        return m_scaleSpin->hasFocus();
    };

    if (anySpinHasFocus())
    {
        return;
    }

    AZ::Vector3 position = AZ::Vector3::CreateZero();
    AZ::Quaternion rotation = AZ::Quaternion::CreateIdentity();
    float scale = 1.0f;
    AZ::TransformBus::EventResult(position, m_runtimeSelection, &AZ::TransformBus::Events::GetWorldTranslation);
    AZ::TransformBus::EventResult(rotation, m_runtimeSelection, &AZ::TransformBus::Events::GetWorldRotationQuaternion);
    AZ::TransformBus::EventResult(scale, m_runtimeSelection, &AZ::TransformBus::Events::GetLocalUniformScale);
    const AZ::Vector3 eulerDegrees = rotation.GetEulerDegrees();

    m_updatingTransformBar = true;
    for (int i = 0; i < 3; ++i)
    {
        m_posSpin[i]->setValue(position.GetElement(i));
        m_rotSpin[i]->setValue(eulerDegrees.GetElement(i));
    }
    m_scaleSpin->setValue(scale);
    m_updatingTransformBar = false;
}

void SceneViewWidget::ApplyTransformBar()
{
    if (m_updatingTransformBar || !GetIEditor()->IsInGameMode() || !RuntimeSelectionValid())
    {
        return;
    }

    const AZ::Vector3 position(
        aznumeric_cast<float>(m_posSpin[0]->value()), aznumeric_cast<float>(m_posSpin[1]->value()),
        aznumeric_cast<float>(m_posSpin[2]->value()));
    const AZ::Vector3 eulerDegrees(
        aznumeric_cast<float>(m_rotSpin[0]->value()), aznumeric_cast<float>(m_rotSpin[1]->value()),
        aznumeric_cast<float>(m_rotSpin[2]->value()));
    const float scale = aznumeric_cast<float>(m_scaleSpin->value());

    AZ::TransformBus::Event(m_runtimeSelection, &AZ::TransformBus::Events::SetWorldTranslation, position);
    AZ::TransformBus::Event(
        m_runtimeSelection, &AZ::TransformBus::Events::SetWorldRotationQuaternion,
        AZ::Quaternion::CreateFromEulerAnglesDegrees(eulerDegrees));
    AZ::TransformBus::Event(m_runtimeSelection, &AZ::TransformBus::Events::SetLocalUniformScale, scale);
}
