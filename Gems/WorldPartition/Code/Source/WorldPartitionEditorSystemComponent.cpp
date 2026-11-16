/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "WorldPartitionEditorSystemComponent.h"
#include "WorldPartitionPrefabProcessor.h"

#include <AzCore/Serialization/SerializeContext.h>

namespace WorldPartition
{
    void WorldPartitionEditorSystemComponent::Reflect(AZ::ReflectContext* context)
    {
        WorldPartitionPrefabProcessor::Reflect(context);
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<WorldPartitionEditorSystemComponent, AZ::Component>()->Version(1);
        }
    }
}
