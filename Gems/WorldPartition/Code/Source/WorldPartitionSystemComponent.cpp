/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "WorldPartitionSystemComponent.h"
#include "WorldPartitionComponent.h"

#include <AzCore/Debug/Trace.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace WorldPartition
{
    void WorldPartitionSystemComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<WorldPartitionSystemComponent, AZ::Component>()->Version(1);
        }
    }
    void WorldPartitionSystemComponent::wp_stats(const AZ::ConsoleCommandContainer&)
    {
        for (const WorldPartitionComponent* partition : WorldPartitionComponent::GetActivePartitions())
        {
            AZ_Printf("WorldPartition", "entity=%s loaded=%u/%zu expandedBounds=%u spawnedEntities=%u\n",
                partition->GetEntity()->GetName().c_str(), partition->GetLoadedCellCount(),
                partition->m_cells.size(), partition->GetExpandedCellBoundsCount(), partition->GetSpawnedEntityCount());
        }
    }
}
