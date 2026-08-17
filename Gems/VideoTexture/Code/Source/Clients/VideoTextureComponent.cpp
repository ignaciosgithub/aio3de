/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "VideoTextureComponent.h"

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/RTTI/BehaviorContext.h>

#include <Atom/RHI/ImagePool.h>
#include <Atom/RHI.Reflect/ImageDescriptor.h>
#include <Atom/RHI.Reflect/ImageSubresource.h>

#define PL_MPEG_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4267) // vendored pl_mpeg: size_t -> long conversion
#endif
#include <External/pl_mpeg.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace VideoTexture
{
    void VideoTextureConfig::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<VideoTextureConfig>()
                ->Version(1)
                ->Field("Video", &VideoTextureConfig::m_video)
                ->Field("TargetTexture", &VideoTextureConfig::m_targetTexture)
                ->Field("PlayOnStart", &VideoTextureConfig::m_playOnStart)
                ->Field("Loop", &VideoTextureConfig::m_loop)
                ->Field("PlaybackSpeed", &VideoTextureConfig::m_playbackSpeed);

            if (auto* editContext = serializeContext->GetEditContext())
            {
                editContext->Class<VideoTextureConfig>("Video Texture Config", "")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &VideoTextureConfig::m_video,
                        "Video", "MPEG-1 video file (.mpg). Convert with: ffmpeg -i in.mp4 -c:v mpeg1video -q:v 5 -an out.mpg")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &VideoTextureConfig::m_targetTexture,
                        "Target texture", "The render target texture (.attimage) the video frames are written to. Assign the same texture to a material to show the video on a mesh.")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &VideoTextureConfig::m_playOnStart,
                        "Play on start", "Start playing as soon as the component activates in game mode")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &VideoTextureConfig::m_loop,
                        "Loop", "Restart the video when it reaches the end")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &VideoTextureConfig::m_playbackSpeed,
                        "Playback speed", "Playback speed multiplier (1 = normal)")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.01f)
                        ->Attribute(AZ::Edit::Attributes::Max, 10.0f);
            }
        }
    }

    VideoTextureComponent::VideoTextureComponent(const VideoTextureConfig& config)
        : m_config(config)
    {
    }

    void VideoTextureComponent::Reflect(AZ::ReflectContext* context)
    {
        VideoTextureConfig::Reflect(context);

        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<VideoTextureComponent, AZ::Component>()
                ->Version(1)
                ->Field("Config", &VideoTextureComponent::m_config);
        }

        if (auto* behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behaviorContext->EBus<VideoTextureRequestBus>("VideoTextureRequestBus")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Category, "VideoTexture")
                ->Event("Play", &VideoTextureRequestBus::Events::Play)
                ->Event("Pause", &VideoTextureRequestBus::Events::Pause)
                ->Event("Stop", &VideoTextureRequestBus::Events::Stop)
                ->Event("IsPlaying", &VideoTextureRequestBus::Events::IsPlaying)
                ->Event("SetLooping", &VideoTextureRequestBus::Events::SetLooping)
                ->Event("GetLooping", &VideoTextureRequestBus::Events::GetLooping)
                ->Event("SetPlaybackSpeed", &VideoTextureRequestBus::Events::SetPlaybackSpeed)
                ->Event("GetPlaybackSpeed", &VideoTextureRequestBus::Events::GetPlaybackSpeed);
        }
    }

    void VideoTextureComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("VideoTextureService"));
    }

    void VideoTextureComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("VideoTextureService"));
    }

    void VideoTextureComponent::Activate()
    {
        VideoTextureRequestBus::Handler::BusConnect(GetEntityId());

        if (m_config.m_video.GetId().IsValid())
        {
            AZ::Data::AssetBus::Handler::BusConnect(m_config.m_video.GetId());
            m_config.m_video.QueueLoad();
        }
    }

    void VideoTextureComponent::Deactivate()
    {
        AZ::TickBus::Handler::BusDisconnect();
        AZ::Data::AssetBus::Handler::BusDisconnect();
        VideoTextureRequestBus::Handler::BusDisconnect();
        ShutdownDecoder();
        m_targetImage = nullptr;
    }

    void VideoTextureComponent::OnAssetReady(AZ::Data::Asset<AZ::Data::AssetData> asset)
    {
        m_config.m_video = asset;
        InitializeDecoder();
    }

    void VideoTextureComponent::InitializeDecoder()
    {
        ShutdownDecoder();

        VideoAsset* videoAsset = m_config.m_video.Get();
        if (!videoAsset || videoAsset->m_data.empty())
        {
            AZ_Warning("VideoTexture", false, "Video asset has no data.");
            return;
        }

        if (!m_config.m_targetTexture.GetId().IsValid())
        {
            AZ_Warning("VideoTexture", false, "No target texture assigned; the video has nowhere to render.");
            return;
        }

        m_targetImage = AZ::RPI::AttachmentImage::FindOrCreate(m_config.m_targetTexture);
        if (!m_targetImage)
        {
            AZ_Warning("VideoTexture", false, "Failed to create the target attachment image.");
            return;
        }

        // The asset owns the buffer; the decoder must not free it.
        m_decoder = plm_create_with_memory(videoAsset->m_data.data(), videoAsset->m_data.size(), 0);
        if (!m_decoder || !plm_probe(m_decoder, videoAsset->m_data.size()))
        {
            AZ_Warning("VideoTexture", false,
                "Could not decode the video: only MPEG-1 video in an MPEG-PS container (.mpg) is supported. "
                "Convert with: ffmpeg -i input.mp4 -c:v mpeg1video -q:v 5 -an output.mpg");
            ShutdownDecoder();
            return;
        }

        plm_set_audio_enabled(m_decoder, 0);
        plm_set_loop(m_decoder, m_config.m_loop ? 1 : 0);
        plm_set_video_decode_callback(m_decoder, &VideoTextureComponent::OnVideoFrameDecoded, this);

        m_videoWidth = aznumeric_cast<AZ::u32>(plm_get_width(m_decoder));
        m_videoHeight = aznumeric_cast<AZ::u32>(plm_get_height(m_decoder));
        m_rgbaBuffer.resize(m_videoWidth * m_videoHeight * 4);

        if (m_config.m_playOnStart)
        {
            Play();
        }
        else
        {
            // Decode and show the first frame so the texture isn't blank.
            plm_decode(m_decoder, 1.0 / AZStd::max(plm_get_framerate(m_decoder), 1.0));
            UploadFrame();
        }
    }

    void VideoTextureComponent::ShutdownDecoder()
    {
        if (m_decoder)
        {
            plm_destroy(m_decoder);
            m_decoder = nullptr;
        }
        m_playing = false;
        m_frameDirty = false;
    }

    void VideoTextureComponent::Play()
    {
        if (!m_decoder)
        {
            return;
        }
        if (plm_has_ended(m_decoder))
        {
            plm_rewind(m_decoder);
        }
        m_playing = true;
        if (!AZ::TickBus::Handler::BusIsConnected())
        {
            AZ::TickBus::Handler::BusConnect();
        }
    }

    void VideoTextureComponent::Pause()
    {
        m_playing = false;
    }

    void VideoTextureComponent::Stop()
    {
        m_playing = false;
        if (m_decoder)
        {
            plm_rewind(m_decoder);
        }
    }

    bool VideoTextureComponent::IsPlaying()
    {
        return m_playing;
    }

    void VideoTextureComponent::SetLooping(bool looping)
    {
        m_config.m_loop = looping;
        if (m_decoder)
        {
            plm_set_loop(m_decoder, looping ? 1 : 0);
        }
    }

    bool VideoTextureComponent::GetLooping()
    {
        return m_config.m_loop;
    }

    void VideoTextureComponent::SetPlaybackSpeed(float speed)
    {
        m_config.m_playbackSpeed = AZStd::clamp(speed, 0.01f, 10.0f);
    }

    float VideoTextureComponent::GetPlaybackSpeed()
    {
        return m_config.m_playbackSpeed;
    }

    void VideoTextureComponent::OnVideoFrameDecoded(plm_t* /*mpeg*/, plm_frame_t* frame, void* user)
    {
        auto* self = static_cast<VideoTextureComponent*>(user);
        plm_frame_to_rgba(frame, self->m_rgbaBuffer.data(), aznumeric_cast<int>(self->m_videoWidth * 4));
        self->m_frameDirty = true;
    }

    void VideoTextureComponent::OnTick(float deltaTime, AZ::ScriptTimePoint /*time*/)
    {
        if (!m_decoder || !m_playing)
        {
            return;
        }

        plm_decode(m_decoder, deltaTime * m_config.m_playbackSpeed);

        if (m_frameDirty)
        {
            UploadFrame();
            m_frameDirty = false;
        }

        if (plm_has_ended(m_decoder))
        {
            m_playing = false;
        }
    }

    void VideoTextureComponent::UploadFrame()
    {
        if (!m_targetImage || m_rgbaBuffer.empty())
        {
            return;
        }

        const AZ::RHI::ImageDescriptor& imageDesc = m_targetImage->GetRHIImage()->GetDescriptor();
        const AZ::u32 uploadWidth = AZStd::min(m_videoWidth, imageDesc.m_size.m_width);
        const AZ::u32 uploadHeight = AZStd::min(m_videoHeight, imageDesc.m_size.m_height);

        if (!m_warnedSizeMismatch && (uploadWidth != imageDesc.m_size.m_width || uploadHeight != imageDesc.m_size.m_height))
        {
            AZ_Warning("VideoTexture", false,
                "Video size (%ux%u) does not match target texture size (%ux%u); the video fills only part of the texture. "
                "Set the .attimage Width/Height to the video's resolution for a full-texture image.",
                m_videoWidth, m_videoHeight, imageDesc.m_size.m_width, imageDesc.m_size.m_height);
            m_warnedSizeMismatch = true;
        }

        constexpr AZ::u32 BytesPerPixel = 4;
        AZ::RHI::ImageUpdateRequest updateRequest;
        AZ::RHI::DeviceImageSubresourceLayout layout{ { uploadWidth, uploadHeight, 1 },
                                                      uploadHeight,
                                                      m_videoWidth * BytesPerPixel,
                                                      m_videoHeight * m_videoWidth * BytesPerPixel,
                                                      1,
                                                      1 };
        updateRequest.m_sourceSubresourceLayout.Init(m_targetImage->GetRHIImage()->GetDeviceMask(), layout);
        updateRequest.m_sourceData = m_rgbaBuffer.data();
        updateRequest.m_image = m_targetImage->GetRHIImage();
        m_targetImage->UpdateImageContents(updateRequest);
    }
} // namespace VideoTexture
