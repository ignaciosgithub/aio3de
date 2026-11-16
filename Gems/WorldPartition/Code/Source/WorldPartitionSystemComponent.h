/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Console/ConsoleFunctor.h>
#include <AzCore/Component/Component.h>

namespace WorldPartition
{
    class WorldPartitionSystemComponent : public AZ::Component
    {
    public:
        AZ_COMPONENT(WorldPartitionSystemComponent, "{778E46CB-1465-45AA-BC19-82B910546C9F}");
        static void Reflect(AZ::ReflectContext* context);
        void Activate() override {}
        void Deactivate() override {}
        void wp_stats(const AZ::ConsoleCommandContainer& arguments);
        AZ_CONSOLEFUNC(WorldPartitionSystemComponent, wp_stats, AZ::ConsoleFunctorFlags::Null,
            "Print loaded cell and entity counts for active world partitions.");
    };
}
