/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <Clients/VideoTextureSystemComponent.h>
#include <Tools/VideoAssetBuilder.h>

namespace VideoTexture
{
    class VideoTextureEditorSystemComponent
        : public VideoTextureSystemComponent
    {
        using BaseSystemComponent = VideoTextureSystemComponent;

    public:
        AZ_COMPONENT(VideoTextureEditorSystemComponent, "{0F21AF3A-3A36-4380-A657-18E55A4440E4}", BaseSystemComponent);

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);
        static void GetDependentServices(AZ::ComponentDescriptor::DependencyArrayType& dependent);

        // AZ::Component
        void Activate() override;
        void Deactivate() override;

    private:
        VideoAssetBuilder m_videoAssetBuilder;
    };
} // namespace VideoTexture
