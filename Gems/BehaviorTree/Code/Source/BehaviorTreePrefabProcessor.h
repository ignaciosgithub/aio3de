/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/std/functional.h>
#include <AzToolsFramework/Prefab/Spawnable/PrefabProcessor.h>

namespace BehaviorTree
{
    class BehaviorTreePrefabProcessor final
        : public AzToolsFramework::Prefab::PrefabConversionUtils::PrefabProcessor
    {
    public:
        AZ_RTTI(
            BehaviorTree::BehaviorTreePrefabProcessor, "{54010604-7256-4D5F-A02D-130EBF82B9ED}",
            AzToolsFramework::Prefab::PrefabConversionUtils::PrefabProcessor);
        AZ_CLASS_ALLOCATOR(BehaviorTreePrefabProcessor, AZ::SystemAllocator);

        using TreeAssetIdLookup = AZStd::function<AZ::Data::AssetId(const AZStd::string& projectRelativePath)>;

        BehaviorTreePrefabProcessor();

        static void Reflect(AZ::ReflectContext* context);
        void Process(AzToolsFramework::Prefab::PrefabConversionUtils::PrefabProcessorContext& context) override;

        TreeAssetIdLookup m_treeAssetIdLookup;
    };
} // namespace BehaviorTree
