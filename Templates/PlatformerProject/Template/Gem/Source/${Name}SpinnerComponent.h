// {BEGIN_LICENSE}
/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
// {END_LICENSE}

#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/Component/TickBus.h>

namespace ${SanitizedCppName}
{
    class ${Name}SpinnerComponent final
        : public AZ::Component
        , private AZ::TickBus::Handler
    {
    public:
        AZ_COMPONENT(${Name}SpinnerComponent, "{1F22311D-39F3-43E3-A51A-AB6F5D90C501}");

        static void Reflect(AZ::ReflectContext* context);

        void Activate() override;
        void Deactivate() override;

    private:
        void OnTick(float deltaTime, AZ::ScriptTimePoint timePoint) override;

        float m_degreesPerSecond = 90.0f;
    };
} // namespace ${SanitizedCppName}
