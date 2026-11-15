/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/EBus/EBus.h>
#include <WorldPartition/WorldPartitionStreamingPlanner.h>

namespace WorldPartition
{
    class WorldPartitionStreamingSourceRequests : public AZ::EBusTraits
    {
    public:
        static constexpr AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Multiple;
        virtual ~WorldPartitionStreamingSourceRequests() = default;
        virtual void GetStreamingSources(AZStd::vector<StreamingSource>& out) = 0;
    };

    using WorldPartitionStreamingSourceRequestBus = AZ::EBus<WorldPartitionStreamingSourceRequests>;
}
