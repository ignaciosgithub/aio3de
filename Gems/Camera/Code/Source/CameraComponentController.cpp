/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "CameraComponentController.h"
#include "AzCore/Component/TransformBus.h"
#include "CameraViewRegistrationBus.h"

#include <Atom/RPI.Public/DynamicDraw/DynamicDrawContext.h>
#include <Atom/RPI.Public/Image/AttachmentImagePool.h>
#include <Atom/RPI.Public/Image/ImageSystemInterface.h>
#include <Atom/RPI.Public/RPIUtils.h>
#include <Atom/RPI.Public/Pass/PassFilter.h>
#include <Atom/RPI.Public/Shader/Shader.h>
#include <Atom/RPI.Public/Shader/ShaderResourceGroup.h>
#include <Atom/RPI.Public/View.h>
#include <Atom/RPI.Public/ViewportContextManager.h>
#include <Atom/RPI.Public/ViewportContext.h>
#include <Atom/RPI.Reflect/Image/AttachmentImageAssetCreator.h>
#include <Atom/RPI.Reflect/ResourcePoolAsset.h>
#include <AtomBridge/PerViewportDynamicDrawInterface.h>

#include <AzCore/Asset/AssetSerializer.h>
#include <AzCore/Component/EntityBus.h>
#include <AzCore/Math/MatrixUtils.h>
#include <AzCore/Math/Vector2.h>

#include <AzFramework/Viewport/ViewportScreen.h>
#include <AzCore/Settings/SettingsRegistry.h>

namespace Camera
{
    void CameraComponentConfig::Reflect(AZ::ReflectContext* context)
    {
        if (auto serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<CameraComponentConfig, AZ::ComponentConfig>()
                ->Version(6)
                ->Field("Orthographic", &CameraComponentConfig::m_orthographic)
                ->Field("Orthographic Half Width", &CameraComponentConfig::m_orthographicHalfWidth)
                ->Field("Field of View", &CameraComponentConfig::m_fov)
                ->Field("Near Clip Plane Distance", &CameraComponentConfig::m_nearClipDistance)
                ->Field("Far Clip Plane Distance", &CameraComponentConfig::m_farClipDistance)
                ->Field("SpecifyDimensions", &CameraComponentConfig::m_specifyFrustumDimensions)
                ->Field("FrustumWidth", &CameraComponentConfig::m_frustumWidth)
                ->Field("FrustumHeight", &CameraComponentConfig::m_frustumHeight)
                ->Field("MakeActiveViewOnActivation", &CameraComponentConfig::m_makeActiveViewOnActivation)
                ->Field("RenderToTexture", &CameraComponentConfig::m_renderTextureAsset)
                ->Field("PipelineTemplate", &CameraComponentConfig::m_pipelineTemplate)
                ->Field("AllowPipelineChange", &CameraComponentConfig::m_allowPipelineChanges)
                ->Field("UpdateRateFps", &CameraComponentConfig::m_updateRateFps)
                ->Field("PipEnabled", &CameraComponentConfig::m_pipEnabled)
                ->Field("PipPosition", &CameraComponentConfig::m_pipPosition)
                ->Field("PipSize", &CameraComponentConfig::m_pipSize)
                ->Field("PipResolutionWidth", &CameraComponentConfig::m_pipResolutionWidth)
                ->Field("PipResolutionHeight", &CameraComponentConfig::m_pipResolutionHeight)
            ;

            if (auto editContext = serializeContext->GetEditContext())
            {
                editContext->Class<CameraComponentConfig>("CameraComponentConfig", "Configuration for a CameraComponent or EditorCameraComponent")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                        ->Attribute(AZ::Edit::Attributes::Visibility, AZ::Edit::PropertyVisibility::ShowChildrenOnly)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &CameraComponentConfig::m_makeActiveViewOnActivation,
                            "Make active camera on activation?", "If true, this camera will become the active render camera when it activates")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &CameraComponentConfig::m_orthographic, "Orthographic",
                        "If set, this camera will use an orthographic projection instead of a perspective one. Objects will appear as the same size, regardless of distance from the camera.")
                        ->Attribute(AZ::Edit::Attributes::ChangeNotify, AZ::Edit::PropertyRefreshLevels::EntireTree)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &CameraComponentConfig::m_orthographicHalfWidth, "Orthographic Half-width", "The half-width used to calculate the orthographic projection. The height will be determined by the aspect ratio.")
                        ->Attribute(AZ::Edit::Attributes::Visibility, &CameraComponentConfig::GetOrthographicParameterVisibility)
                        ->Attribute(AZ::Edit::Attributes::Min, 0.001f)
                        ->Attribute(AZ::Edit::Attributes::ChangeNotify, AZ::Edit::PropertyRefreshLevels::ValuesOnly)

                    ->DataElement(AZ::Edit::UIHandlers::Default, &CameraComponentConfig::m_fov, "Field of view", "Vertical field of view in degrees. Note: Max FoV is less than 180.")
                        ->Attribute(AZ::Edit::Attributes::Min, MinFoV)
                        ->Attribute(AZ::Edit::Attributes::Suffix, " degrees")
                        ->Attribute(AZ::Edit::Attributes::Step, 1.f)
                        ->Attribute(AZ::Edit::Attributes::Max, AZ::RadToDeg(AZ::Constants::Pi) - 0.001f)       //We assert at fovs >= Pi so set the max for this field to be just under that
                        ->Attribute(AZ::Edit::Attributes::ChangeNotify, AZ::Edit::PropertyRefreshLevels::ValuesOnly)
                        ->Attribute(AZ::Edit::Attributes::Visibility, &CameraComponentConfig::GetPerspectiveParameterVisibility)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &CameraComponentConfig::m_nearClipDistance, "Near clip distance",
                        "Distance to the near clip plane of the view Frustum")
                        ->Attribute(AZ::Edit::Attributes::Min, MinimumNearPlaneDistance)
                        ->Attribute(AZ::Edit::Attributes::Suffix, " m")
                        ->Attribute(AZ::Edit::Attributes::Step, 0.1f)
                        ->Attribute(AZ::Edit::Attributes::Max, &CameraComponentConfig::GetFarClipDistance)
                        ->Attribute(AZ::Edit::Attributes::ChangeNotify, AZ::Edit::PropertyRefreshLevels::AttributesAndValues)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &CameraComponentConfig::m_farClipDistance, "Far clip distance",
                        "Distance to the far clip plane of the view Frustum")
                        ->Attribute(AZ::Edit::Attributes::Min, &CameraComponentConfig::GetNearClipDistance)
                        ->Attribute(AZ::Edit::Attributes::Suffix, " m")
                        ->Attribute(AZ::Edit::Attributes::Step, 10.f)
                        ->Attribute(AZ::Edit::Attributes::ChangeNotify, AZ::Edit::PropertyRefreshLevels::AttributesAndValues)
                    ->ClassElement(AZ::Edit::ClassElements::Group, "Render To Texture")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &CameraComponentConfig::m_renderTextureAsset, "Target texture", "The render target texture which the camera renders to.")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &CameraComponentConfig::m_pipelineTemplate, "Pipeline template", "The root pass template for the camera's render pipeline")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &CameraComponentConfig::m_allowPipelineChanges, "Allow pipeline changes", "If true, the camera's render pipeline can be changed at runtime.")
                        ->Attribute(AZ::Edit::Attributes::Visibility, &CameraComponentConfig::GetAllowPipelineChangesVisibility)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &CameraComponentConfig::m_updateRateFps, "Update rate",
                        "Maximum renders per second when this camera targets a texture or picture-in-picture. 0 renders every frame. Values below 1 are allowed (0.5 = one render every 2 seconds).")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Suffix, " fps")
                    ->ClassElement(AZ::Edit::ClassElements::Group, "Picture In Picture")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &CameraComponentConfig::m_pipEnabled, "Picture-in-picture",
                        "Draw this camera's view as an overlay rectangle on top of the main view (e.g. minimap, rear-view mirror). Works in the editor viewport and in game.")
                        ->Attribute(AZ::Edit::Attributes::ChangeNotify, AZ::Edit::PropertyRefreshLevels::EntireTree)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &CameraComponentConfig::m_pipPosition, "Overlay position",
                        "Top-left corner of the overlay in normalized screen coordinates (0,0 = top-left, 1,1 = bottom-right).")
                        ->Attribute(AZ::Edit::Attributes::Visibility, &CameraComponentConfig::GetPipParameterVisibility)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &CameraComponentConfig::m_pipSize, "Overlay size",
                        "Size of the overlay in normalized screen coordinates (0.3,0.3 covers 30% of the screen in each direction).")
                        ->Attribute(AZ::Edit::Attributes::Visibility, &CameraComponentConfig::GetPipParameterVisibility)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &CameraComponentConfig::m_pipResolutionWidth, "Resolution width",
                        "Horizontal resolution the camera renders at. Lower resolutions render faster.")
                        ->Attribute(AZ::Edit::Attributes::Min, 16u)
                        ->Attribute(AZ::Edit::Attributes::Visibility, &CameraComponentConfig::GetPipParameterVisibility)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &CameraComponentConfig::m_pipResolutionHeight, "Resolution height",
                        "Vertical resolution the camera renders at. Lower resolutions render faster.")
                        ->Attribute(AZ::Edit::Attributes::Min, 16u)
                        ->Attribute(AZ::Edit::Attributes::Visibility, &CameraComponentConfig::GetPipParameterVisibility)
                ;
            }
        }
    }
    AZ::u32 CameraComponentConfig::GetPipParameterVisibility() const
    {
        return m_pipEnabled ? AZ::Edit::PropertyVisibility::Show : AZ::Edit::PropertyVisibility::Hide;
    }

    AZ::u32 CameraComponentConfig::GetAllowPipelineChangesVisibility() const
    {
        bool experimentalFeaturesEnabled = false;
        const auto* registry = AZ::SettingsRegistry::Get();
        if (registry)
        {
            registry->Get(experimentalFeaturesEnabled, "/O3DE/Atom/ExperimentalFeaturesEnabled");
        }
        return experimentalFeaturesEnabled ? AZ::Edit::PropertyVisibility::Show : AZ::Edit::PropertyVisibility::Hide;
    }

    float CameraComponentConfig::GetFarClipDistance() const
    {
        return m_farClipDistance;
    }

    float CameraComponentConfig::GetNearClipDistance() const
    {
        return m_nearClipDistance;
    }

    AZ::EntityId CameraComponentConfig::GetEditorEntityId() const
    {
        return AZ::EntityId(m_editorEntityId);
    }

    AZ::u32 CameraComponentConfig::GetPerspectiveParameterVisibility() const
    {
        return m_orthographic ? AZ::Edit::PropertyVisibility::Hide : AZ::Edit::PropertyVisibility::Show;
    }

    AZ::u32 CameraComponentConfig::GetOrthographicParameterVisibility() const
    {
        return m_orthographic ? AZ::Edit::PropertyVisibility::Show : AZ::Edit::PropertyVisibility::Hide;
    }

    CameraComponentController::CameraComponentController(const CameraComponentConfig& config)
    {
        SetConfiguration(config);
    }

    void CameraComponentController::ActivateAtomView()
    {
        auto atomViewportRequests = AZ::Interface<AZ::RPI::ViewportContextRequestsInterface>::Get();
        if (atomViewportRequests)
        {
            AZ_Assert(GetView(), "Attempted to activate Atom camera before component activation");

            const AZ::Name contextName = atomViewportRequests->GetDefaultViewportContextName();

            AZ::RPI::ViewportContextNotificationBus::Handler::BusConnect(contextName);

            // Ensure the Atom camera is updated with our current transform state
            AZ::Transform localTransform;
            AZ::TransformBus::EventResult(localTransform, m_entityId, &AZ::TransformBus::Events::GetLocalTM);
            AZ::Transform worldTransform;
            AZ::TransformBus::EventResult(worldTransform, m_entityId, &AZ::TransformBus::Events::GetWorldTM);
            OnTransformChanged(localTransform, worldTransform);

            // Push the Atom camera after we make sure we're up-to-date with our component's transform to ensure the viewport reads the correct state
            UpdateCamera();
            atomViewportRequests->PushViewGroup(contextName, m_atomCameraViewGroup);
        }
    }

    void CameraComponentController::DeactivateAtomView()
    {
        if (auto atomViewportRequests = AZ::Interface<AZ::RPI::ViewportContextRequestsInterface>::Get())
        {
            const AZ::Name contextName = atomViewportRequests->GetDefaultViewportContextName();
            atomViewportRequests->PopViewGroup(contextName, m_atomCameraViewGroup);

            // Picture-in-picture cameras keep listening for render ticks to draw their overlay
            if (!(m_config.m_pipEnabled && m_pipImage))
            {
                AZ::RPI::ViewportContextNotificationBus::Handler::BusDisconnect(contextName);
            }
        }
    }

    void CameraComponentController::SetShouldActivateFunction(AZStd::function<bool()> shouldActivateFunction)
    {
        m_shouldActivateFn = AZStd::move(shouldActivateFunction);
    }

    void CameraComponentController::SetIsLockedFunction(AZStd::function<bool()> isLockedFunction)
    {
        m_isLockedFn = AZStd::move(isLockedFunction);
    }

    void CameraComponentController::Reflect(AZ::ReflectContext* context)
    {
        CameraComponentConfig::Reflect(context);

        if (auto serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<CameraComponentController>()
                ->Version(1)
                ->Field("Configuration", &CameraComponentController::m_config)
            ;

            if (auto editContext = serializeContext->GetEditContext())
            {
                editContext->Class<CameraComponentController>("CameraComponentController", "Controller for a CameraComponent or EditorCameraComponent")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                        ->Attribute(AZ::Edit::Attributes::Visibility, AZ::Edit::PropertyVisibility::ShowChildrenOnly)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &CameraComponentController::m_config, "Configuration", "Camera Controller Configuration")
                ;
            }
        }
    }

    void CameraComponentController::GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required)
    {
        required.push_back(AZ_CRC_CE("TransformService"));
    }

    void CameraComponentController::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("CameraService"));
    }

    void CameraComponentController::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("CameraService"));
    }

    void CameraComponentController::Init()
    {
        AZStd::function<void(AZ::RPI::ViewPtr view)> onChange = [this](AZ::RPI::ViewPtr view)
        {
            if (!m_updatingTransformFromEntity && !m_isLockedFn())
            {
                AZ::TransformBus::Event(m_entityId, &AZ::TransformInterface::SetWorldTM, view->GetCameraTransform());
            }
        };
        m_atomCameraViewGroup = AZStd::make_shared<AZ::RPI::ViewGroup>();
        m_atomCameraViewGroup->Init(AZ::RPI::ViewGroup::Descriptor{ onChange, nullptr });
        
        if (auto rpiSystemInterface = AZ::RPI::RPISystemInterface::Get())
        {
            m_xrSystem = rpiSystemInterface->GetXRSystem();
            if (m_xrSystem)
            {
                m_numSterescopicViews = m_xrSystem->GetNumViews();
                AZ_Assert(m_numSterescopicViews <= AZ::RPI::XRMaxNumViews, "Atom only supports two XR views");
            }
        }
    }

    void CameraComponentController::Activate(AZ::EntityId entityId)
    {
        m_entityId = entityId;

        // Let's set the camera default transforms:
        AZ::TransformBus::EventResult(m_xrCameraToBaseSpaceTm, m_entityId, &AZ::TransformBus::Handler::GetWorldTM);
        m_xrBaseSpaceToHeadTm = AZ::Transform::CreateIdentity();
        m_xrHeadToLeftEyeTm = AZ::Transform::CreateIdentity();
        m_xrHeadToRightEyeTm = AZ::Transform::CreateIdentity();

        AZ::RPI::XRSpaceNotificationBus::Handler::BusConnect();

        auto atomViewportRequests = AZ::Interface<AZ::RPI::ViewportContextRequestsInterface>::Get();
        
        if (atomViewportRequests)
        {
            const AZ::EntityId editorEntityId = m_config.GetEditorEntityId();

            // Lazily create our camera as part of Activate
            // This will be persisted as part of our config so that it may be shared between the Editor & Game components.
            // Also, when using the Editor and opening the level for the first time,  GetViewForEntity() will return a null ViewPtr.
            if (GetView() == nullptr && editorEntityId.IsValid())
            {
                AZ::RPI::ViewPtr atomEditorView = nullptr;
                CameraViewRegistrationRequestsBus::BroadcastResult(atomEditorView, &CameraViewRegistrationRequests::GetViewForEntity, editorEntityId);
                m_atomCameraViewGroup->SetView(atomEditorView, AZ::RPI::ViewType::Default);
            }

            AZStd::string entityName;
            AZ::ComponentApplicationBus::BroadcastResult(entityName, &AZ::ComponentApplicationBus::Events::GetEntityName, m_entityId);
            AZ::Name cameraName = AZ::Name(AZStd::string::format("%s View", entityName.c_str()));
            // If there wasn't already a view registered (or the registration system isn't available), create a View
            if (GetView() == nullptr)
            {
                m_atomCameraViewGroup->CreateMainView(cameraName);
                if (editorEntityId.IsValid())
                {
                    CameraViewRegistrationRequestsBus::Broadcast(
                        &CameraViewRegistrationRequests::SetViewForEntity,
                        editorEntityId,
                        GetView());
                }
            }
            m_atomCameraViewGroup->CreateStereoscopicViews(cameraName);

            AZ::RPI::ViewProviderBus::Handler::BusConnect(m_entityId);
            m_atomCameraViewGroup->Activate();
        }

        // OnTransformChanged() is only called if the camera is actually moved, so make sure we call it at least once,
        // so the camera-transform is also correct for static cameras even before they are made the active view
        AZ::Transform local, world;
        AZ::TransformBus::Event(entityId, &AZ::TransformBus::Events::GetLocalAndWorld, local, world);
        OnTransformChanged(local, world);

        CameraRequestBus::Handler::BusConnect(m_entityId);
        AZ::TransformNotificationBus::Handler::BusConnect(m_entityId);
        CameraBus::Handler::BusConnect();
        CameraNotificationBus::Broadcast(&CameraNotificationBus::Events::OnCameraAdded, m_entityId);

        if (m_config.m_pipEnabled)
        {
            CreatePipTarget();
        }

        if (m_config.m_renderTextureAsset.GetId().IsValid() || m_pipTargetAsset.GetId().IsValid())
        {
            CreateRenderPipelineForTexture();
        }

        if (m_renderToTexturePipeline && m_config.m_updateRateFps > 0.0f)
        {
            AZ::TickBus::Handler::BusConnect();
        }

        if (m_config.m_pipEnabled && m_pipImage)
        {
            if (atomViewportRequests)
            {
                AZ::RPI::ViewportContextNotificationBus::Handler::BusConnect(atomViewportRequests->GetDefaultViewportContextName());
            }
        }

        // Only activate if we're configured to do so, and our activation call back indicates that we should
        if (m_config.m_makeActiveViewOnActivation && (!m_shouldActivateFn || m_shouldActivateFn()))
        {
            MakeActiveView();
        }
    }

    void CameraComponentController::Deactivate()
    {
        AZ::RPI::XRSpaceNotificationBus::Handler::BusDisconnect();
        AZ::TickBus::Handler::BusDisconnect();
        AZ::RPI::ViewportContextNotificationBus::Handler::BusDisconnect();

        if (m_renderToTexturePipeline)
        {
            auto scene = AZ::RPI::RPISystemInterface::Get()->GetSceneByName(AZ::Name("Main"));
            scene->RemoveRenderPipeline(m_renderToTexturePipeline->GetId());
            m_renderToTexturePipeline = nullptr;
        }
        m_pipImage = nullptr;
        m_pipTargetAsset.Release();

        CameraNotificationBus::Broadcast(&CameraNotificationBus::Events::OnCameraRemoved, m_entityId);
        CameraBus::Handler::BusDisconnect();
        AZ::TransformNotificationBus::Handler::BusDisconnect(m_entityId);
        CameraRequestBus::Handler::BusDisconnect(m_entityId);
        AZ::RPI::ViewProviderBus::Handler::BusDisconnect(m_entityId);
        m_atomCameraViewGroup->Deactivate();
       
        DeactivateAtomView();
    }

    void CameraComponentController::CreateRenderPipelineForTexture()
    {
        auto scene = AZ::RPI::RPISystemInterface::Get()->GetSceneByName(AZ::Name("Main"));

        const AZStd::string pipelineName = "Camera_" + m_entityId.ToString() + "_Pipeline";

        AZ::RPI::RenderPipelineDescriptor pipelineDesc;
        pipelineDesc.m_mainViewTagName = "MainCamera";
        pipelineDesc.m_name = pipelineName;
        pipelineDesc.m_rootPassTemplate = m_config.m_pipelineTemplate;
        pipelineDesc.m_renderSettings.m_multisampleState = AZ::RPI::RPISystemInterface::Get()->GetApplicationMultisampleState();
        pipelineDesc.m_allowModification = m_config.m_allowPipelineChanges;
        const AZ::Data::Asset<AZ::RPI::AttachmentImageAsset>& targetAsset =
            m_pipTargetAsset.GetId().IsValid() ? m_pipTargetAsset : m_config.m_renderTextureAsset;
        m_renderToTexturePipeline = AZ::RPI::RenderPipeline::CreateRenderPipelineForImage(pipelineDesc, targetAsset);

        if (!m_renderToTexturePipeline)
        {
            AZStd::string entityName;
            AZ::ComponentApplicationBus::BroadcastResult(entityName, &AZ::ComponentApplicationRequests::GetEntityName, m_entityId);
            AZ_Error("Camera", false, "Failed to create render to texture pipeline for camera component in entity %s", entityName.c_str());
            return;
        }
        scene->AddRenderPipeline(m_renderToTexturePipeline);
        m_renderToTexturePipeline->SetDefaultView(GetView());

        if (m_config.m_updateRateFps > 0.0f)
        {
            // Rate-limited cameras only render when OnTick schedules them.
            m_renderToTexturePipeline->RemoveFromRenderTick();
            m_renderToTexturePipeline->AddToRenderTickOnce();
            m_renderAccumulator = 0.0f;
        }
    }

    void CameraComponentController::CreatePipTarget()
    {
        auto imageSystem = AZ::RPI::ImageSystemInterface::Get();
        if (!imageSystem)
        {
            return;
        }

        const AZ::Data::Instance<AZ::RPI::AttachmentImagePool>& pool = imageSystem->GetSystemAttachmentPool();
        if (!pool)
        {
            return;
        }

        const AZ::u32 width = AZStd::max<AZ::u32>(m_config.m_pipResolutionWidth, 16);
        const AZ::u32 height = AZStd::max<AZ::u32>(m_config.m_pipResolutionHeight, 16);

        AZ::RHI::ImageDescriptor imageDesc = AZ::RHI::ImageDescriptor::Create2D(
            AZ::RHI::ImageBindFlags::Color | AZ::RHI::ImageBindFlags::ShaderReadWrite,
            width,
            height,
            AZ::RHI::Format::R8G8B8A8_UNORM);

        const AZ::Data::AssetId assetId(AZ::Uuid::CreateRandom());
        const AZ::Name imageName(AZStd::string::format("CameraPip_%s", m_entityId.ToString().c_str()));

        AZ::RPI::AttachmentImageAssetCreator imageAssetCreator;
        imageAssetCreator.Begin(assetId);
        imageAssetCreator.SetImageDescriptor(imageDesc);
        imageAssetCreator.SetPoolAsset({ pool->GetAssetId(), azrtti_typeid<AZ::RPI::ResourcePoolAsset>() });
        imageAssetCreator.SetName(imageName, false);

        if (!imageAssetCreator.End(m_pipTargetAsset))
        {
            AZ_Error("Camera", false, "Failed to create the picture-in-picture render target for camera entity %s", m_entityId.ToString().c_str());
            return;
        }

        m_pipImage = AZ::RPI::AttachmentImage::FindOrCreate(m_pipTargetAsset);
        if (!m_pipImage)
        {
            AZ_Error("Camera", false, "Failed to create the picture-in-picture image for camera entity %s", m_entityId.ToString().c_str());
            m_pipTargetAsset.Release();
        }
    }

    void CameraComponentController::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        if (m_renderToTexturePipeline && m_config.m_updateRateFps > 0.0f)
        {
            m_renderAccumulator += deltaTime;
            const float interval = 1.0f / m_config.m_updateRateFps;
            if (m_renderAccumulator >= interval)
            {
                m_renderAccumulator = AZStd::min(m_renderAccumulator - interval, interval);
                m_renderToTexturePipeline->AddToRenderTickOnce();
            }
        }
    }

    void CameraComponentController::OnRenderTick()
    {
        if (m_config.m_pipEnabled && m_pipImage)
        {
            DrawPipOverlay();
        }
    }

    void CameraComponentController::DrawPipOverlay()
    {
        static const AZ::Name pipDrawContextName("CameraPipOverlay");
        static constexpr const char* pipShaderPath = "Shaders/SimpleTextured.azshader";

        auto perViewportDynamicDraw = AZ::AtomBridge::PerViewportDynamicDraw::Get();
        auto viewportContextManager = AZ::RPI::ViewportContextRequests::Get();
        if (!perViewportDynamicDraw || !viewportContextManager)
        {
            return;
        }

        AZ::RPI::ViewportContextPtr viewportContext = viewportContextManager->GetDefaultViewportContext();
        if (!viewportContext)
        {
            return;
        }

        static bool s_pipDrawContextRegistered = false;
        if (!s_pipDrawContextRegistered)
        {
            AZ::Data::Instance<AZ::RPI::Shader> shader = AZ::RPI::LoadCriticalShader(pipShaderPath);
            if (!shader)
            {
                return;
            }
            perViewportDynamicDraw->RegisterDynamicDrawContext(
                pipDrawContextName,
                [shader](AZ::RHI::Ptr<AZ::RPI::DynamicDrawContext> dynamicDraw)
                {
                    dynamicDraw->InitShader(shader);
                    dynamicDraw->InitVertexFormat(
                        { { "POSITION", AZ::RHI::Format::R32G32B32_FLOAT },
                          { "COLOR", AZ::RHI::Format::B8G8R8A8_UNORM },
                          { "TEXCOORD0", AZ::RHI::Format::R32G32_FLOAT } });
                    dynamicDraw->AddDrawStateOptions(AZ::RPI::DynamicDrawContext::DrawStateOptions::ShaderVariant);
                    dynamicDraw->EndInit();
                });
            s_pipDrawContextRegistered = true;
        }

        AZ::RHI::Ptr<AZ::RPI::DynamicDrawContext> dynamicDraw =
            perViewportDynamicDraw->GetDynamicDrawContextForViewport(pipDrawContextName, viewportContext->GetId());
        if (!dynamicDraw || !dynamicDraw->IsReady())
        {
            return;
        }

        const auto [viewportWidth, viewportHeight] = viewportContext->GetViewportSize();
        const float width = aznumeric_cast<float>(viewportWidth);
        const float height = aznumeric_cast<float>(viewportHeight);

        const float x0 = m_config.m_pipPosition.GetX() * width;
        const float y0 = m_config.m_pipPosition.GetY() * height;
        const float x1 = x0 + m_config.m_pipSize.GetX() * width;
        const float y1 = y0 + m_config.m_pipSize.GetY() * height;

        struct PipVertex
        {
            float m_position[3];
            AZ::u32 m_color;
            float m_uv[2];
        };

        constexpr AZ::u32 white = 0xFFFFFFFF;
        const PipVertex vertices[4] = {
            { { x0, y0, 0.5f }, white, { 0.0f, 0.0f } },
            { { x1, y0, 0.5f }, white, { 1.0f, 0.0f } },
            { { x1, y1, 0.5f }, white, { 1.0f, 1.0f } },
            { { x0, y1, 0.5f }, white, { 0.0f, 1.0f } },
        };
        const AZ::u16 indices[6] = { 0, 1, 2, 0, 2, 3 };

        AZ::RPI::ShaderOptionList shaderOptions;
        shaderOptions.push_back(AZ::RPI::ShaderOption(AZ::Name("o_clamp"), AZ::Name("true")));
        shaderOptions.push_back(AZ::RPI::ShaderOption(AZ::Name("o_useColorChannels"), AZ::Name("true")));
        dynamicDraw->SetShaderVariant(dynamicDraw->UseShaderVariant(shaderOptions));

        AZ::Data::Instance<AZ::RPI::ShaderResourceGroup> drawSrg = dynamicDraw->NewDrawSrg();
        if (!drawSrg)
        {
            return;
        }

        const AZ::RHI::ShaderResourceGroupLayout* layout = drawSrg->GetLayout();
        const auto imageInputIndex = layout->FindShaderInputImageIndex(AZ::Name("m_texture"));
        const auto viewProjInputIndex = layout->FindShaderInputConstantIndex(AZ::Name("m_worldToProj"));

        AZ::Matrix4x4 modelViewProjMat;
        AZ::MakeOrthographicMatrixRH(modelViewProjMat, 0.0f, width, height, 0.0f, 1.0f, 0.0f);

        drawSrg->SetImageView(imageInputIndex, m_pipImage->GetImageView());
        drawSrg->SetConstant(viewProjInputIndex, modelViewProjMat);
        drawSrg->Compile();

        dynamicDraw->DrawIndexed(vertices, 4, indices, 6, AZ::RHI::IndexFormat::Uint16, drawSrg);
    }

    void CameraComponentController::SetConfiguration(const CameraComponentConfig& config)
    {
        m_config = config;
        UpdateCamera();
    }

    const CameraComponentConfig& CameraComponentController::GetConfiguration() const
    {
        return m_config;
    }

    AZ::RPI::ViewportContextPtr CameraComponentController::GetViewportContext()
    {
        if (auto atomViewportRequests = AZ::Interface<AZ::RPI::ViewportContextRequestsInterface>::Get();
            m_atomCameraViewGroup && atomViewportRequests)
        {
            return atomViewportRequests->GetDefaultViewportContext();
        }
        return nullptr;
    }

    AZ::EntityId CameraComponentController::GetCameras()
    {
        return m_entityId;
    }

    float CameraComponentController::GetFovDegrees()
    {
        return m_config.m_fov;
    }

    float CameraComponentController::GetFovRadians()
    {
        return AZ::DegToRad(m_config.m_fov);
    }

    float CameraComponentController::GetNearClipDistance()
    {
        return m_config.m_nearClipDistance;
    }

    float CameraComponentController::GetFarClipDistance()
    {
        return m_config.m_farClipDistance;
    }

    float CameraComponentController::GetFrustumWidth()
    {
        return m_config.m_frustumWidth;
    }

    float CameraComponentController::GetFrustumHeight()
    {
        return m_config.m_frustumHeight;
    }

    bool CameraComponentController::IsOrthographic()
    {
        return m_config.m_orthographic;
    }

    float CameraComponentController::GetOrthographicHalfWidth()
    {
        return m_config.m_orthographicHalfWidth;
    }

    void CameraComponentController::SetFovDegrees(float fov)
    {
        m_config.m_fov = AZ::GetClamp(fov, MinFoV, MaxFoV);
        UpdateCamera();
    }

    void CameraComponentController::SetFovRadians(float fov)
    {
        SetFovDegrees(AZ::RadToDeg(fov));
    }

    void CameraComponentController::SetNearClipDistance(float nearClipDistance)
    {
        const float clampedNear =
            AZ::GetClamp(nearClipDistance, MinimumNearPlaneDistance, m_config.m_farClipDistance - MinimumNearPlaneDistance);
        AZ_Warning(
            "CameraComponent", clampedNear == nearClipDistance,
            "Unable to set near clip distance to %f: value must be in [%f, far clip - %f]. Clamped to %f.",
            nearClipDistance, MinimumNearPlaneDistance, MinimumNearPlaneDistance, clampedNear);
        m_config.m_nearClipDistance = clampedNear;
        UpdateCamera();
    }

    void CameraComponentController::SetFarClipDistance(float farClipDistance)
    {
        const float clampedFar = AZ::GetMax(farClipDistance, m_config.m_nearClipDistance + MinimumNearPlaneDistance);
        AZ_Warning(
            "CameraComponent", clampedFar == farClipDistance,
            "Unable to set far clip distance to %f: value must be greater than the near clip distance. Clamped to %f.",
            farClipDistance, clampedFar);
        m_config.m_farClipDistance = clampedFar;
        UpdateCamera();
    }

    void CameraComponentController::SetFrustumWidth(float width)
    {
        m_config.m_frustumWidth = width;
        UpdateCamera();
    }

    void CameraComponentController::SetFrustumHeight(float height)
    {
        m_config.m_frustumHeight = height;
        UpdateCamera();
    }

    void CameraComponentController::SetOrthographic(bool orthographic)
    {
        m_config.m_orthographic = orthographic;
        UpdateCamera();
    }

    void CameraComponentController::SetOrthographicHalfWidth(float halfWidth)
    {
        m_config.m_orthographicHalfWidth = halfWidth;
        UpdateCamera();
    }

    void CameraComponentController::SetXRViewQuaternion([[maybe_unused]] const AZ::Quaternion& viewQuat, [[maybe_unused]] uint32_t xrViewIndex)
    {
        //No implementation needed as we are calling into CR system directly to get view data within OnTransformChanged
    }

    void CameraComponentController::MakeActiveView()
    {
        if (IsActiveView())
        {
            return;
        }

        // Set Atom camera, if it exists
        if (m_atomCameraViewGroup->IsAnyViewEnabled())
        {
            ActivateAtomView();
        }

        // Update camera parameters
        UpdateCamera();

        // Notify of active view changed
        CameraNotificationBus::Broadcast(&CameraNotificationBus::Events::OnActiveViewChanged, m_entityId);
    }

    bool CameraComponentController::IsActiveView()
    {
        return m_isActiveView;
    }

    namespace Util
    {
        AZ::Vector3 GetWorldPosition(const AZ::Vector3& origin, float depth, const AzFramework::CameraState& cameraState)
        {
            if (depth == 0.f)
            {
                return origin;
            }
            else
            {
                const AZ::Vector3 rayDirection = cameraState.m_orthographic ? cameraState.m_forward : (origin - cameraState.m_position);
                return origin + (rayDirection.GetNormalized() * depth);
            }
        }
    }

    AZ::Vector3 CameraComponentController::ScreenToWorld(const AZ::Vector2& screenPosition, float depth)
    {
        const AzFramework::ScreenPoint point{ static_cast<int>(screenPosition.GetX()), static_cast<int>(screenPosition.GetY()) };
        const AzFramework::CameraState& cameraState = GetCameraState();
        const AZ::Vector3 origin = AzFramework::ScreenToWorld(point, cameraState);
        return Util::GetWorldPosition(origin, depth, cameraState);
    }

    AZ::Vector3 CameraComponentController::ScreenNdcToWorld(const AZ::Vector2& screenNdcPosition, float depth)
    {
        const AzFramework::CameraState& cameraState = GetCameraState();
        const AZ::Vector3 origin = AzFramework::ScreenNdcToWorld(screenNdcPosition, AzFramework::InverseCameraView(cameraState), AzFramework::InverseCameraProjection(cameraState));
        return Util::GetWorldPosition(origin, depth, cameraState);
    }

    AZ::Vector2 CameraComponentController::WorldToScreenNdc(const AZ::Vector3& worldPosition)
    {
        const AzFramework::CameraState& cameraState = GetCameraState();
        const AZ::Vector3 screenPosition = AzFramework::WorldToScreenNdc(worldPosition, AzFramework::CameraView(cameraState), AzFramework::CameraProjection(cameraState));
        return AZ::Vector2(screenPosition); 
    }

    AZ::Vector2 CameraComponentController::WorldToScreen(const AZ::Vector3& worldPosition)
    {
        const AzFramework::ScreenPoint& point = AzFramework::WorldToScreen(worldPosition, GetCameraState());
        return AZ::Vector2(static_cast<float>(point.m_x), static_cast<float>(point.m_y));
    }

    AzFramework::CameraState CameraComponentController::GetCameraState()
    {
        auto viewportContext = GetViewportContext();
        if (!m_atomCameraViewGroup->IsAnyViewEnabled() || !viewportContext)
        {
            return AzFramework::CameraState();
        }

        const auto windowSize = viewportContext->GetViewportSize();
        const auto viewportSize = AzFramework::ScreenSize(windowSize.m_width, windowSize.m_height);

        AzFramework::CameraState cameraState =
            AzFramework::CreateDefaultCamera(GetView()->GetCameraTransform(), viewportSize);
        AzFramework::SetCameraClippingVolumeFromPerspectiveFovMatrixRH(
            cameraState, GetView()->GetViewToClipMatrix());

        return cameraState;
    }

    void CameraComponentController::OnTransformChanged([[maybe_unused]] const AZ::Transform& local, const AZ::Transform& world)
    {
        if (m_updatingTransformFromEntity)
        {
            return;
        }

        m_updatingTransformFromEntity = true;

        if (m_xrSystem && m_xrSystem->ShouldRender())
        {
            // When the XR System is active, The camera world transform will always be:
            // camWorldTm = m_xrCameraToBaseSpaceTm * m_xrBaseSpaceToHeadTm.
            // But when OnTransformChanged is called, may be because a Lua Script is changing
            // the camera location, we need to apply the inverse operation to preserve the value
            // of m_xrCameraToBaseSpaceTm.
            // This is the quick math:
            // m_xrCameraToBaseSpaceTm~ * camWorldTm = m_xrCameraToBaseSpaceTm~ * m_xrCameraToBaseSpaceTm * m_xrBaseSpaceToHeadTm
            // m_xrCameraToBaseSpaceTm~ * camWorldTm = m_xrBaseSpaceToHeadTm
            // m_xrCameraToBaseSpaceTm~ * camWorldTm * camWorldTm~ = m_xrBaseSpaceToHeadTm * camWorldTm~
            // m_xrCameraToBaseSpaceTm~ = m_xrBaseSpaceToHeadTm * camWorldTm~
            // m_xrCameraToBaseSpaceTm~~ = (m_xrBaseSpaceToHeadTm * camWorldTm~)~
            // m_xrCameraToBaseSpaceTm = camWorldTm~~ * m_xrBaseSpaceToHeadTm~
            // m_xrCameraToBaseSpaceTm = camWorldTm * m_xrBaseSpaceToHeadTm~
            m_xrCameraToBaseSpaceTm = world * m_xrBaseSpaceToHeadTm.GetInverse();
            m_updatingTransformFromEntity = false;
            // We are not going to call m_atomCameraViewGroup->SetCameraTransform() yet.
            // We need to wait for the OnXRSpaceLocationsChanged() notification, which will give us
            // the XR Headset orientation.
            return;
        }
        else
        {
            m_atomCameraViewGroup->SetCameraTransform(AZ::Matrix3x4::CreateFromTransform(world.GetOrthogonalized()));
        }

        m_updatingTransformFromEntity = false;

        UpdateCamera();
    }

    void CameraComponentController::OnViewportSizeChanged([[maybe_unused]] AzFramework::WindowSize size)
    {
        if (IsActiveView())
        {
            UpdateCamera();
        }
    }

    void CameraComponentController::OnViewportDefaultViewChanged(AZ::RPI::ViewPtr view)
    {
        m_isActiveView = GetView() == view;
    }

    AZ::RPI::ViewPtr CameraComponentController::GetView() const
    {
        return m_atomCameraViewGroup->GetView(AZ::RPI::ViewType::Default);
    }

    AZ::RPI::ViewPtr CameraComponentController::GetStereoscopicView(AZ::RPI::ViewType viewType) const
    {
        return m_atomCameraViewGroup->GetView(viewType);
    }

    void CameraComponentController::UpdateCamera()
    {
        // O3de assumes a setup for reversed depth
        const bool reverseDepth = true;

        if (auto viewportContext = GetViewportContext())
        {
            AZ::Matrix4x4 viewToClipMatrix;
            if (!m_atomAuxGeom)
            {
                SetupAtomAuxGeom(viewportContext);
            }
            auto windowSize = viewportContext->GetViewportSize();
            const float aspectRatio = aznumeric_cast<float>(windowSize.m_width) / aznumeric_cast<float>(windowSize.m_height);

            // Guard against invalid clip planes (zero/negative near, or far <= near) which
            // produce a degenerate projection matrix and crash downstream rendering code.
            const float nearDistance = AZ::GetMax(m_config.m_nearClipDistance, MinimumNearPlaneDistance);
            const float farDistance = AZ::GetMax(m_config.m_farClipDistance, nearDistance + MinimumNearPlaneDistance);

            // This assumes a reversed depth buffer, in line with other LY Atom integration
            if (m_config.m_orthographic)
            {
                AZ::MakeOrthographicMatrixRH(viewToClipMatrix,
                    -m_config.m_orthographicHalfWidth,
                    m_config.m_orthographicHalfWidth,
                    -m_config.m_orthographicHalfWidth / aspectRatio,
                    m_config.m_orthographicHalfWidth / aspectRatio,
                    nearDistance,
                    farDistance,
                    reverseDepth);
            }
            else
            {
                AZ::MakePerspectiveFovMatrixRH(viewToClipMatrix,
                    AZ::DegToRad(m_config.m_fov),
                    aspectRatio,
                    nearDistance,
                    farDistance,
                    reverseDepth);
            }
            m_updatingTransformFromEntity = true;
            m_atomCameraViewGroup->SetViewToClipMatrix(viewToClipMatrix);

            // Update stereoscopic projection matrix
            if (m_xrSystem && m_xrSystem->ShouldRender())
            {
                AZ::Matrix4x4 projection = AZ::Matrix4x4::CreateIdentity();
                for (AZ::u32 i = 0; i < m_numSterescopicViews; i++)
                {
                    AZ::RPI::ViewType viewType = i == 0 ? AZ::RPI::ViewType::XrLeft : AZ::RPI::ViewType::XrRight;
                    AZ::RPI::FovData fovData;
                    m_xrSystem->GetViewFov(i, fovData);

                    if ( (fovData.m_angleLeft != 0 || fovData.m_angleRight != 0) && (fovData.m_angleUp != 0 || fovData.m_angleDown != 0))
                    {
                        projection = m_xrSystem->CreateStereoscopicProjection(
                            fovData.m_angleLeft,
                            fovData.m_angleRight,
                            fovData.m_angleDown,
                            fovData.m_angleUp,
                            m_config.m_nearClipDistance,
                            m_config.m_farClipDistance,
                            reverseDepth);
                        m_atomCameraViewGroup->SetStereoscopicViewToClipMatrix(projection, reverseDepth, viewType);
                    }
                }
            } 
            m_updatingTransformFromEntity = false;
        }
    }

    void CameraComponentController::SetupAtomAuxGeom(AZ::RPI::ViewportContextPtr viewportContext)
    {
        if (auto scene = viewportContext->GetRenderScene())
        {
            if (auto auxGeomFP = scene->GetFeatureProcessor<AZ::RPI::AuxGeomFeatureProcessorInterface>())
            {
                m_atomAuxGeom = auxGeomFP->GetOrCreateDrawQueueForView(GetView().get());
            }
        }
    }

    ////////////////////////////////////////////////////////////////////
    // AZ::RPI::XRSpaceNotificationBus::Handler Overrides
    void CameraComponentController::OnXRSpaceLocationsChanged(
        const AZ::Transform& baseSpaceToHeadTm,
        const AZ::Transform& headToleftEyeTm,
        const AZ::Transform& headToRightEyeTm)
    {
        if (!m_xrSystem || !m_xrSystem->ShouldRender())
        {
            return;
        }

        m_updatingTransformFromEntity = true;

        m_xrBaseSpaceToHeadTm = baseSpaceToHeadTm;
        const auto mainCameraWorldTm = m_xrCameraToBaseSpaceTm * baseSpaceToHeadTm;

        AZ::TransformBus::Event(m_entityId, &AZ::TransformBus::Handler::SetWorldTM, mainCameraWorldTm);

        // Update camera world matrix for the main pipeline.
        m_atomCameraViewGroup->SetCameraTransform(AZ::Matrix3x4::CreateFromTransform(mainCameraWorldTm));

        // Update camera world matrix for the left eye pipeline.
        m_xrHeadToLeftEyeTm = headToleftEyeTm;
        const auto leftEyeWorldTm = mainCameraWorldTm * headToleftEyeTm;
        m_atomCameraViewGroup->SetCameraTransform(AZ::Matrix3x4::CreateFromTransform(leftEyeWorldTm), AZ::RPI::ViewType::XrLeft);

        // Update camera world matrix for the right eye pipeline.
        m_xrHeadToRightEyeTm = headToRightEyeTm;
        const auto rightEyeWorldTm = mainCameraWorldTm * headToRightEyeTm;
        m_atomCameraViewGroup->SetCameraTransform(AZ::Matrix3x4::CreateFromTransform(rightEyeWorldTm), AZ::RPI::ViewType::XrRight);

        UpdateCamera();

        m_updatingTransformFromEntity = false;
    }
    ////////////////////////////////////////////////////////////////////

} //namespace Camera
