/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/Component/Component.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/std/containers/vector.h>

#include <Atom/RPI.Public/Image/AttachmentImage.h>
#include <Atom/RPI.Reflect/Image/AttachmentImageAsset.h>

#include <VideoTexture/VideoAsset.h>
#include <VideoTexture/VideoTextureBus.h>

#if defined(_MSC_VER)
#pragma warning(push, 0) // vendored pl_mpeg: don't apply warning-as-error to third-party code
#endif
#include <External/pl_mpeg.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace VideoTexture
{
    //! Serialized settings for the Video Texture component.
    class VideoTextureConfig final
    {
    public:
        AZ_TYPE_INFO(VideoTextureConfig, "{4571340B-60EF-42F6-B95E-2397B4AE66E5}");

        static void Reflect(AZ::ReflectContext* context);

        AZ::Data::Asset<VideoAsset> m_video{ AZ::Data::AssetLoadBehavior::QueueLoad };
        AZ::Data::Asset<AZ::RPI::AttachmentImageAsset> m_targetTexture{ AZ::Data::AssetLoadBehavior::QueueLoad };
        bool m_playOnStart = true;
        bool m_loop = true;
        float m_playbackSpeed = 1.0f;
    };

    //! Decodes an MPEG-1 video and streams its frames onto an attachment image (render target
    //! texture), so any material referencing that texture displays the video in game.
    class VideoTextureComponent final
        : public AZ::Component
        , private AZ::TickBus::Handler
        , private AZ::Data::AssetBus::Handler
        , private VideoTextureRequestBus::Handler
    {
    public:
        AZ_COMPONENT(VideoTextureComponent, "{4A58F6BE-4603-4781-8AC1-37A7625465CE}");

        VideoTextureComponent() = default;
        explicit VideoTextureComponent(const VideoTextureConfig& config);

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);

        // AZ::Component
        void Activate() override;
        void Deactivate() override;

        // VideoTextureRequestBus
        void Play() override;
        void Pause() override;
        void Stop() override;
        bool IsPlaying() override;
        void SetLooping(bool looping) override;
        bool GetLooping() override;
        void SetPlaybackSpeed(float speed) override;
        float GetPlaybackSpeed() override;

    private:
        // AZ::TickBus
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;

        // AZ::Data::AssetBus
        void OnAssetReady(AZ::Data::Asset<AZ::Data::AssetData> asset) override;

        void InitializeDecoder();
        void ShutdownDecoder();
        void UploadFrame();

        static void OnVideoFrameDecoded(plm_t* mpeg, plm_frame_t* frame, void* user);

        VideoTextureConfig m_config;
        plm_t* m_decoder = nullptr;
        AZ::Data::Instance<AZ::RPI::AttachmentImage> m_targetImage;
        AZStd::vector<AZ::u8> m_rgbaBuffer;
        AZ::u32 m_videoWidth = 0;
        AZ::u32 m_videoHeight = 0;
        bool m_playing = false;
        bool m_frameDirty = false;
        bool m_warnedSizeMismatch = false;
    };
} // namespace VideoTexture
