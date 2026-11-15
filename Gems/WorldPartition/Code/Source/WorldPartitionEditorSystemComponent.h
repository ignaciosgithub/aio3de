/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Component/Component.h>

namespace WorldPartition
{
    class WorldPartitionEditorSystemComponent : public AZ::Component
    {
    public:
        AZ_COMPONENT(WorldPartitionEditorSystemComponent, "{B6AEB143-5EB4-460C-9DAB-B1D1F11E3356}");
        static void Reflect(AZ::ReflectContext* context);
        void Activate() override {}
        void Deactivate() override {}
    };
}
