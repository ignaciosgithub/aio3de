/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include "BehaviorTreeDefinition.h"

#include <AzCore/Outcome/Outcome.h>
#include <AzCore/std/string/string_view.h>

namespace BehaviorTree
{
    class BehaviorTreeParser
    {
    public:
        using ParseOutcome = AZ::Outcome<BehaviorTreeDefinition, AZStd::string>;

        static ParseOutcome Parse(AZStd::string_view json);
        static ParseOutcome LoadFromFile(const AZStd::string& path);
    };
} // namespace BehaviorTree
