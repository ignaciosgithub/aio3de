/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Math/Vector3.h>
#include <AzCore/RTTI/RTTI.h>
#include <AzCore/RTTI/ReflectContext.h>

namespace SoftBodyPhysics
{
    //! What the soft body particles collide against.
    enum class SoftBodyCollisionMode : AZ::u8
    {
        Simple = 0,        //!< Ground plane only (cheapest).
        World = 1,         //!< Static physics colliders in the level (raycast scene queries).
        WorldAndRigid = 2  //!< Static colliders plus dynamic rigid bodies (two-way: the soft body is pushed and pushes back).
    };

    //! User-tunable soft body settings, edited in the component UI.
    struct SoftBodySettings
    {
        AZ_TYPE_INFO(SoftBodySettings, "{5B7C3E01-9D2A-4F6E-A1B8-3C4D5E6F7A80}");

        static void Reflect(AZ::ReflectContext* context);

        float m_massPerVertex = 0.1f;      //!< Mass of each simulated vertex in kg.
        float m_compliance = 0.001f;       //!< Edge softness; 0 = rigid edges, larger = stretchier.
        float m_pressure = 1.0f;           //!< Volume preservation; 0 = off, 1 = keep rest volume, > 1 inflates.
        float m_damping = 0.5f;            //!< Per-second velocity damping [0..1].
        AZ::u32 m_substeps = 4;            //!< Simulation substeps per frame.
        AZ::u32 m_iterations = 4;          //!< Constraint iterations per substep.
        float m_gravityScale = 1.0f;       //!< Multiplier on world gravity (-9.81 m/s^2 Z).
        SoftBodyCollisionMode m_collisionMode = SoftBodyCollisionMode::Simple; //!< Simple ground plane or world static colliders.
        float m_particleRadius = 0.02f;    //!< Collision thickness of each particle in World mode (meters).
        float m_worldFriction = 0.5f;      //!< Tangential friction on world collider contact [0..1].
        float m_rigidPushScale = 1.0f;     //!< Scale on the impulse applied to dynamic rigid bodies on contact.
        float m_rigidMaxPushVelocity = 2.0f; //!< Max velocity change (m/s) a single contact can impart on a rigid body.
        bool m_softSoftCollision = false;  //!< Collide with the particles of other soft bodies in the level.
        float m_softSoftFriction = 0.5f;   //!< Tangential friction on soft-soft particle contact [0..1].
        bool m_groundCollision = true;     //!< Collide against a world-space horizontal plane.
        float m_groundHeight = 0.0f;       //!< World Z of the ground plane.
        float m_groundFriction = 0.5f;     //!< Tangential friction on ground contact [0..1].
        bool m_pinHighestVertices = false; //!< Pin the topmost vertices (hanging bodies).
        float m_pinTolerance = 0.01f;      //!< Vertices within this distance of the top are pinned (meters).
    };
} // namespace SoftBodyPhysics

namespace AZ
{
    AZ_TYPE_INFO_SPECIALIZE(SoftBodyPhysics::SoftBodyCollisionMode, "{9C6E1F42-7A8B-4C0D-9E2F-1A3B5C7D9E0F}");
}
