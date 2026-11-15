/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/RTTI/TypeInfo.h>

namespace WorldPartition
{
    enum class WorldPartitionPolicy : AZ::u8
    {
        Default,
        AlwaysLoaded,
        Streamed
    };
    AZ_TYPE_INFO_SPECIALIZE(WorldPartitionPolicy, "{F76FBE6D-4888-4C1F-BA70-AFE80ECF54BA}");

    class WorldPartitionPolicyComponent : public AZ::Component
    {
    public:
        AZ_COMPONENT(WorldPartitionPolicyComponent, "{B6B75140-94D2-4F52-94FD-62E2222B07F2}");
        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);
        void Activate() override {}
        void Deactivate() override {}
        WorldPartitionPolicy m_policy = WorldPartitionPolicy::Default;
    };
}
