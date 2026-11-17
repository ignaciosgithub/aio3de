// {BEGIN_LICENSE}
/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
// {END_LICENSE}

#include "${Name}SpinnerComponent.h"

#include <AzCore/Component/TransformBus.h>
#include <AzCore/Math/MathUtils.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace ${SanitizedCppName}
{
    void ${Name}SpinnerComponent::Reflect(AZ::ReflectContext* context)
    {
        auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context);
        if (!serializeContext)
        {
            return;
        }

        serializeContext->Class<${Name}SpinnerComponent, AZ::Component>()
            ->Version(1)
            ->Field("DegreesPerSecond", &${Name}SpinnerComponent::m_degreesPerSecond);

        if (AZ::EditContext* editContext = serializeContext->GetEditContext())
        {
            editContext->Class<${Name}SpinnerComponent>("${Name} Spinner", "Rotates this entity around its local Z axis")
                ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Category, "${Name}")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                ->DataElement(AZ::Edit::UIHandlers::Default, &${Name}SpinnerComponent::m_degreesPerSecond,
                    "Degrees per second", "Rotation speed around local Z");
        }
    }

    void ${Name}SpinnerComponent::Activate()
    {
        AZ::TickBus::Handler::BusConnect();
    }

    void ${Name}SpinnerComponent::Deactivate()
    {
        AZ::TickBus::Handler::BusDisconnect();
    }

    void ${Name}SpinnerComponent::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint timePoint)
    {
        AZ::Vector3 localRotation = AZ::Vector3::CreateZero();
        AZ::TransformBus::EventResult(localRotation, GetEntityId(), &AZ::TransformBus::Events::GetLocalRotation);
        localRotation.SetZ(localRotation.GetZ() + AZ::DegToRad(m_degreesPerSecond * deltaTime));
        AZ::TransformBus::Event(GetEntityId(), &AZ::TransformBus::Events::SetLocalRotation, localRotation);
    }
} // namespace ${SanitizedCppName}
