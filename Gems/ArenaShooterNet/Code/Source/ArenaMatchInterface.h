/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzCore/Component/EntityId.h>
#include <AzCore/RTTI/RTTI.h>

namespace ArenaShooterNet
{
    //! Payload for the server-local "Damage" gameplay event, carrying the
    //! attacker so kills can be attributed for scoring. Health components
    //! also accept a plain float payload (unattributed damage).
    struct ArenaDamage
    {
        AZ_TYPE_INFO(ArenaDamage, "{E1B7A9D4-5C20-4F8B-9E36-7A48D2C1F053}");

        float m_damage = 0.0f;
        AZ::EntityId m_attacker;
    };

    //! Server-side interface to the match controller, registered with
    //! AZ::Interface by the authority NetworkArenaMatchComponent while a
    //! match entity exists in the level. All calls are server-local.
    class IArenaMatch
    {
    public:
        AZ_RTTI(IArenaMatch, "{8C4B1F6A-2D93-4E07-B5A8-91F3C6D0247E}");
        virtual ~IArenaMatch() = default;

        //! False during warm-up and map-vote downtime: damage is ignored.
        virtual bool IsCombatEnabled() const = 0;

        //! Attribute a kill to an attacker (called by health on death).
        virtual void ReportKill(AZ::EntityId attacker, AZ::EntityId victim) = 0;

        //! Register a map vote for the current vote phase (one vote per
        //! voter; re-voting replaces the previous choice). Ignored outside
        //! the vote phase.
        virtual void SubmitMapVote(AZ::EntityId voter, uint8_t mapIndex) = 0;
    };
} // namespace ArenaShooterNet
