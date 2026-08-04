/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include "NetworkArenaMatchComponent.h"

#include <AzCore/Console/IConsole.h>
#include <AzCore/Console/ILogger.h>
#include <AzCore/Interface/Interface.h>
#include <AzCore/StringFunc/StringFunc.h>
#include <Multiplayer/IMultiplayer.h>
#include <Multiplayer/NetworkEntity/INetworkEntityManager.h>

namespace ArenaShooterNet
{
    NetworkArenaMatchComponentController::NetworkArenaMatchComponentController(NetworkArenaMatchComponent& parent)
        : NetworkArenaMatchComponentControllerBase(parent)
    {
    }

    void NetworkArenaMatchComponentController::OnActivate([[maybe_unused]] Multiplayer::EntityIsMigrating entityIsMigrating)
    {
        if (IsNetEntityRoleAuthority())
        {
            if (AZ::Interface<IArenaMatch>::Get() == nullptr)
            {
                AZ::Interface<IArenaMatch>::Register(this);
                m_registered = true;
            }
            else
            {
                AZLOG_WARN("ArenaMatch: another match controller is already active; this one stays idle");
            }
            EnterPhase(MatchPhase::Warmup);
            AZ::TickBus::Handler::BusConnect();
        }
    }

    void NetworkArenaMatchComponentController::OnDeactivate([[maybe_unused]] Multiplayer::EntityIsMigrating entityIsMigrating)
    {
        AZ::TickBus::Handler::BusDisconnect();
        if (m_registered)
        {
            AZ::Interface<IArenaMatch>::Unregister(this);
            m_registered = false;
        }
    }

    bool NetworkArenaMatchComponentController::IsCombatEnabled() const
    {
        return static_cast<MatchPhase>(GetPhase()) == MatchPhase::Live;
    }

    void NetworkArenaMatchComponentController::ReportKill(AZ::EntityId attacker, [[maybe_unused]] AZ::EntityId victim)
    {
        if (static_cast<MatchPhase>(GetPhase()) != MatchPhase::Live || !attacker.IsValid())
        {
            return;
        }

        const uint32_t kills = ++m_kills[attacker];
        if (kills > GetLeadingScore())
        {
            SetLeadingScore(kills);
            const Multiplayer::NetEntityId netId =
                Multiplayer::GetNetworkEntityManager()->GetNetEntityIdById(attacker);
            SetWinnerNetId(aznumeric_cast<uint32_t>(netId));
        }

        if (GetScoreLimit() > 0 && kills >= GetScoreLimit())
        {
            AZLOG_INFO("ArenaMatch: score limit reached (%u kills) — match over", kills);
            EnterPhase(MatchPhase::MapVote);
        }
    }

    void NetworkArenaMatchComponentController::SubmitMapVote(AZ::EntityId voter, uint8_t mapIndex)
    {
        if (static_cast<MatchPhase>(GetPhase()) != MatchPhase::MapVote || !voter.IsValid())
        {
            return;
        }
        const AZStd::vector<AZStd::string> maps = ParseMapList();
        if (mapIndex >= maps.size())
        {
            return;
        }
        m_votes[voter] = mapIndex;
        UpdateWinningVote();
    }

    void NetworkArenaMatchComponentController::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        const MatchPhase phase = static_cast<MatchPhase>(GetPhase());

        // a duration of zero means "untimed" for the live phase only
        if (phase == MatchPhase::Live && GetMatchDuration() <= 0.0f)
        {
            return;
        }

        m_phaseTimer -= deltaTime;
        SetPhaseTimeRemaining(AZStd::max(m_phaseTimer, 0.0f));
        if (m_phaseTimer > 0.0f)
        {
            return;
        }

        switch (phase)
        {
        case MatchPhase::Warmup:
            EnterPhase(MatchPhase::Live);
            break;
        case MatchPhase::Live:
            AZLOG_INFO("ArenaMatch: time limit reached — match over");
            EnterPhase(MatchPhase::MapVote);
            break;
        case MatchPhase::MapVote:
            EndVoteAndCycle();
            break;
        }
    }

    void NetworkArenaMatchComponentController::EnterPhase(MatchPhase phase)
    {
        SetPhase(aznumeric_cast<uint8_t>(phase));
        switch (phase)
        {
        case MatchPhase::Warmup:
            m_phaseTimer = GetWarmupDuration();
            m_kills.clear();
            m_votes.clear();
            SetLeadingScore(0);
            SetWinnerNetId(0);
            SetWinningMapIndex(0);
            break;
        case MatchPhase::Live:
            m_phaseTimer = GetMatchDuration();
            break;
        case MatchPhase::MapVote:
            m_phaseTimer = GetVoteDuration();
            m_votes.clear();
            break;
        }
        SetPhaseTimeRemaining(AZStd::max(m_phaseTimer, 0.0f));
    }

    void NetworkArenaMatchComponentController::UpdateWinningVote()
    {
        AZStd::unordered_map<uint8_t, uint32_t> tally;
        for (const auto& [voter, mapIndex] : m_votes)
        {
            ++tally[mapIndex];
        }
        uint8_t winner = 0;
        uint32_t best = 0;
        for (const auto& [mapIndex, count] : tally)
        {
            if (count > best || (count == best && mapIndex < winner))
            {
                best = count;
                winner = mapIndex;
            }
        }
        SetWinningMapIndex(winner);
    }

    void NetworkArenaMatchComponentController::EndVoteAndCycle()
    {
        const AZStd::vector<AZStd::string> maps = ParseMapList();
        const uint8_t winner = GetWinningMapIndex();

        if (!maps.empty() && winner < maps.size())
        {
            // the Multiplayer gem forwards the server's level load to every
            // connected client, so a plain LoadLevel cycles the whole match
            const AZStd::string command = AZStd::string("LoadLevel ") + maps[winner];
            AZLOG_INFO("ArenaMatch: map vote won by '%s'", maps[winner].c_str());
            if (auto* console = AZ::Interface<AZ::IConsole>::Get())
            {
                console->PerformCommand(command.c_str());
                return;
            }
        }

        // no map list (or no console): restart the flow on the current level
        EnterPhase(MatchPhase::Warmup);
    }

    AZStd::vector<AZStd::string> NetworkArenaMatchComponentController::ParseMapList() const
    {
        AZStd::vector<AZStd::string> maps;
        AZ::StringFunc::Tokenize(GetMapList().c_str(), maps, ',', false, true);
        for (AZStd::string& map : maps)
        {
            AZ::StringFunc::TrimWhiteSpace(map, true, true);
        }
        return maps;
    }
} // namespace ArenaShooterNet
