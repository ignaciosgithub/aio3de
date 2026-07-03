/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Math/SoftBody.h>

namespace SoftBodyPhysics
{
    //! Tracks the active soft bodies in the level so they can collide with each other.
    //! All access happens on the main thread (soft bodies tick on TICK_PHYSICS).
    namespace SoftBodyRegistry
    {
        void Register(AZ::SoftBody* body, float particleRadius);
        void Unregister(AZ::SoftBody* body);

        //! Resolves particle contacts between \p body and every other registered soft body.
        void SolveContacts(AZ::SoftBody* body, float particleRadius, float friction);
    } // namespace SoftBodyRegistry
} // namespace SoftBodyPhysics
