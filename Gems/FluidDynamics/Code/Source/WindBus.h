/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/EBus/EBus.h>
#include <AzCore/Math/Vector3.h>

namespace FluidDynamics
{
    //! Global wind sampling: all active Wind components add their contribution.
    class WindRequests : public AZ::EBusTraits
    {
    public:
        static constexpr AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Multiple;
        static constexpr AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::Single;

        //! Adds this handler's wind velocity at \p position into \p accumulated.
        virtual void AccumulateWind(const AZ::Vector3& position, AZ::Vector3& accumulated) const = 0;

    protected:
        ~WindRequests() = default;
    };
    using WindRequestBus = AZ::EBus<WindRequests>;

    //! Total wind velocity from every active Wind component at \p position.
    inline AZ::Vector3 SampleTotalWind(const AZ::Vector3& position)
    {
        AZ::Vector3 wind = AZ::Vector3::CreateZero();
        WindRequestBus::Broadcast(&WindRequests::AccumulateWind, position, wind);
        return wind;
    }
} // namespace FluidDynamics
