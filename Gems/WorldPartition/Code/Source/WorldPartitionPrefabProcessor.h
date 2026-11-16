/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzToolsFramework/Prefab/Spawnable/PrefabProcessor.h>

namespace WorldPartition
{
    class WorldPartitionPrefabProcessor final
        : public AzToolsFramework::Prefab::PrefabConversionUtils::PrefabProcessor
    {
    public:
        AZ_RTTI(WorldPartitionPrefabProcessor, "{C2B7210E-BDB4-45E0-96E3-0D4AB452A933}",
            AzToolsFramework::Prefab::PrefabConversionUtils::PrefabProcessor);
        AZ_CLASS_ALLOCATOR(WorldPartitionPrefabProcessor, AZ::SystemAllocator);
        static void Reflect(AZ::ReflectContext* context);
        void Process(AzToolsFramework::Prefab::PrefabConversionUtils::PrefabProcessorContext& context) override;
    };
}
