/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Component/Component.h>
#include <WorldPartition/WorldPartitionStreamingSourceBus.h>

namespace WorldPartition
{
    class WorldPartitionStreamingSourceComponent
        : public AZ::Component
        , private WorldPartitionStreamingSourceRequestBus::Handler
    {
    public:
        AZ_COMPONENT(WorldPartitionStreamingSourceComponent, "{F9DD67D1-75D5-4E01-A253-985E60226EB9}");
        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);
        void Activate() override;
        void Deactivate() override;
        float m_loadRadius = 0.0f;
    private:
        void GetStreamingSources(AZStd::vector<StreamingSource>& out) override;
    };
}
