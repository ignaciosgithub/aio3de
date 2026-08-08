/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <Source/AutoGen/NetworkArenaMatchComponent.AutoComponent.h>

#include <AzCore/Component/TickBus.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>

#include <Source/ArenaMatchInterface.h>

namespace ArenaShooterNet
{
    //! Server-authoritative match flow controller. Place one on a
    //! network-bound level entity. Runs the warm-up -> live -> map-vote state
    //! machine, tracks kills, ends the match on the score limit or timer,
    //! and loads the vote-winning map. Phase, time remaining, leading score
    //! and winner replicate to every client for HUD display.
    class NetworkArenaMatchComponentController
        : public NetworkArenaMatchComponentControllerBase
        , public IArenaMatch
        , private AZ::TickBus::Handler
    {
    public:
        explicit NetworkArenaMatchComponentController(NetworkArenaMatchComponent& parent);

        // NetworkArenaMatchComponentControllerBase
        void OnActivate(Multiplayer::EntityIsMigrating entityIsMigrating) override;
        void OnDeactivate(Multiplayer::EntityIsMigrating entityIsMigrating) override;

        // IArenaMatch (server-local)
        bool IsCombatEnabled() const override;
        void ReportKill(AZ::EntityId attacker, AZ::EntityId victim) override;
        void SubmitMapVote(AZ::EntityId voter, uint8_t mapIndex) override;

        enum class MatchPhase : uint8_t
        {
            Warmup = 0,
            Live = 1,
            MapVote = 2,
        };

    private:
        // AZ::TickBus (server only)
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;

        void EnterPhase(MatchPhase phase);
        void EndVoteAndCycle();
        void UpdateWinningVote();
        AZStd::vector<AZStd::string> ParseMapList() const;

        float m_phaseTimer = 0.0f;
        AZStd::unordered_map<AZ::EntityId, uint32_t> m_kills;
        AZStd::unordered_map<AZ::EntityId, uint8_t> m_votes;
        bool m_registered = false;
    };
} // namespace ArenaShooterNet
