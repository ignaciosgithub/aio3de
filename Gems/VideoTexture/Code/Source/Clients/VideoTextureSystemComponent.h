/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>

#include "VideoAssetHandler.h"

namespace VideoTexture
{
    class VideoTextureSystemComponent
        : public AZ::Component
    {
    public:
        AZ_COMPONENT(VideoTextureSystemComponent, "{6EDCF8C1-6D30-4947-8CC5-6E4F6FA4F333}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);

        // AZ::Component
        void Activate() override;
        void Deactivate() override;

    private:
        AZStd::unique_ptr<VideoAssetHandler> m_videoAssetHandler;
    };
} // namespace VideoTexture
