/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "BehaviorTreePrefabProcessor.h"

#include "BehaviorTreeComponent.h"
#include "BehaviorTreePaths.h"

#include <AzCore/Asset/AssetManagerBus.h>
#include <AzCore/Debug/Trace.h>
#include <AzCore/IO/Path/Path.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/containers/unordered_set.h>
#include <AzCore/std/string/string_view.h>
#include <AzCore/std/utility/move.h>
#include <AzCore/Utils/Utils.h>
#include <AzFramework/Spawnable/Spawnable.h>
#include <AzFramework/Spawnable/SpawnableAssetHandler.h>
#include <AzToolsFramework/API/EditorAssetSystemAPI.h>
#include <AzToolsFramework/Prefab/Spawnable/PrefabDocument.h>
#include <AzToolsFramework/Prefab/Spawnable/PrefabProcessorContext.h>

#include <algorithm>

namespace BehaviorTree
{
    using AzToolsFramework::Prefab::PrefabConversionUtils::PrefabDocument;
    using AzToolsFramework::Prefab::PrefabConversionUtils::PrefabProcessor;
    using AzToolsFramework::Prefab::PrefabConversionUtils::PrefabProcessorContext;

    namespace
    {
        AZ::Data::AssetId FindTreeAssetId(const AZStd::string& projectRelativePath)
        {
            const AZ::IO::Path fullSourcePath = AZ::IO::Path(AZ::Utils::GetProjectPath()) / projectRelativePath;
            const AZStd::string fullSourcePathString = fullSourcePath.String();

            AZ::Data::AssetInfo sourceInfo;
            AZStd::string watchFolder;
            bool sourceInfoFound = false;
            AzToolsFramework::AssetSystemRequestBus::BroadcastResult(
                sourceInfoFound, &AzToolsFramework::AssetSystemRequestBus::Events::GetSourceInfoBySourcePath,
                fullSourcePathString.c_str(), sourceInfo, watchFolder);
            if (!sourceInfoFound || sourceInfo.m_assetId.m_guid.IsNull())
            {
                return {};
            }

            // Null-type JSON copy products use the default subId inferred by AssetBuilderSDK.
            return AZ::Data::AssetId(sourceInfo.m_assetId.m_guid, 0);
        }
    } // namespace

    BehaviorTreePrefabProcessor::BehaviorTreePrefabProcessor()
        : m_treeAssetIdLookup(FindTreeAssetId)
    {
    }

    void BehaviorTreePrefabProcessor::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<BehaviorTreePrefabProcessor, PrefabProcessor>()->Version(1);
        }
    }

    void BehaviorTreePrefabProcessor::Process(PrefabProcessorContext& context)
    {
        context.ListPrefabs([this, &context](PrefabDocument& document)
        {
            AZStd::vector<AZStd::string> treePaths;
            AZStd::unordered_set<AZStd::string> uniqueTreePaths;
            document.GetInstance().GetAllEntitiesInHierarchy([&](AZStd::unique_ptr<AZ::Entity>& entity)
            {
                if (entity)
                {
                    if (const BehaviorTreeComponent* component = entity->FindComponent<BehaviorTreeComponent>())
                    {
                        const AZStd::vector<AZStd::string> candidates = GetTreePathCandidates(component->GetTreeFile());
                        constexpr AZStd::string_view projectRootPrefix = "@projectroot@/";
                        if (candidates.size() > 1 && candidates[0].find(projectRootPrefix) == 0)
                        {
                            AZStd::string projectRelativePath = candidates[0].substr(projectRootPrefix.size());
                            if (uniqueTreePaths.insert(projectRelativePath).second)
                            {
                                treePaths.push_back(AZStd::move(projectRelativePath));
                            }
                        }
                    }
                }
                return true;
            });

            std::sort(treePaths.begin(), treePaths.end());
            const AZStd::string spawnableProductName =
                document.GetName() + AzFramework::Spawnable::DotFileExtension;
            const AZ::Data::AssetId spawnableAssetId(
                context.GetSourceUuid(), AzFramework::SpawnableAssetHandler::BuildSubId(spawnableProductName));

            for (const AZStd::string& treePath : treePaths)
            {
                const AZ::Data::AssetId treeAssetId =
                    m_treeAssetIdLookup ? m_treeAssetIdLookup(treePath) : AZ::Data::AssetId{};
                if (!treeAssetId.IsValid())
                {
                    AZ_Warning("BehaviorTree", false, "Could not resolve tree product asset for '%s' in prefab '%s'",
                        treePath.c_str(), document.GetName().c_str());
                    continue;
                }

                context.RegisterProductAssetDependency(
                    spawnableAssetId, treeAssetId, AZ::Data::AssetLoadBehavior::NoLoad);
            }
        });
    }
} // namespace BehaviorTree
