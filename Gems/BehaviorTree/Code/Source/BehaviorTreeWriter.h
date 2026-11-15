/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include "BehaviorTreeDefinition.h"

#include <AzCore/std/string/string.h>

namespace BehaviorTree
{
    class BehaviorTreeWriter
    {
    public:
        static AZStd::string Write(const BehaviorTreeDefinition& definition);
    };
} // namespace BehaviorTree
