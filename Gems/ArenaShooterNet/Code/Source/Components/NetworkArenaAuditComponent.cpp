/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include "NetworkArenaAuditComponent.h"

#include <AzCore/Component/TransformBus.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/math.h>
#include <cstring>
#include <AzCore/Console/ILogger.h>
#include <AzCore/Interface/Interface.h>
#include <AzCore/Name/Name.h>
#include <AzNetworking/Framework/INetworking.h>
#include <Multiplayer/Components/NetBindComponent.h>
#include <Multiplayer/MultiplayerConstants.h>

#include <Source/AutoGen/NetworkArenaHealthComponent.AutoComponent.h>

#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

namespace ArenaShooterNet
{
    namespace
    {
        const StartingPointInput::InputEventNotificationId AuditMoveForwardEventId("MoveForward");
        const StartingPointInput::InputEventNotificationId AuditMoveRightEventId("MoveRight");
        const StartingPointInput::InputEventNotificationId AuditLookXEventId("LookX");
        const StartingPointInput::InputEventNotificationId AuditLookYEventId("LookY");
        const StartingPointInput::InputEventNotificationId AuditShootEventId("Shoot");

        constexpr AZ::u64 FnvOffsetBasis = 14695981039346656037ull;
        constexpr AZ::u64 FnvPrime = 1099511628211ull;

        AZ::u64 FnvFold(AZ::u64 hash, const void* data, size_t size)
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < size; ++i)
            {
                hash = (hash ^ bytes[i]) * FnvPrime;
            }
            return hash;
        }

        bool ConstantTimeEquals(
            const NetworkArenaAuditComponentController::AuditTag& lhs,
            const NetworkArenaAuditComponentController::AuditTag& rhs)
        {
            AZ::u64 diff = 0;
            for (size_t i = 0; i < lhs.size(); ++i)
            {
                diff |= lhs[i] ^ rhs[i];
            }
            return diff == 0;
        }
    } // namespace

    NetworkArenaAuditComponentController::NetworkArenaAuditComponentController(NetworkArenaAuditComponent& parent)
        : NetworkArenaAuditComponentControllerBase(parent)
    {
    }

    void NetworkArenaAuditComponentController::OnActivate([[maybe_unused]] Multiplayer::EntityIsMigrating entityIsMigrating)
    {
#if AZ_TRAIT_SERVER
        if (IsNetEntityRoleAuthority())
        {
            // fresh per-session audit key from the OS CSPRNG, delivered over the
            // (DTLS-protected) reliable channel
            if (RAND_bytes(reinterpret_cast<unsigned char*>(m_key.data()), int(m_key.size() * sizeof(AZ::u64))) == 1)
            {
                m_keySet = true;
                SetAuditKey(m_key[0], m_key[1], m_key[2], m_key[3]);
                m_random.SetSeed(m_key[0] ^ m_key[3]);
                ScheduleNextChallenge();
                AZ::TickBus::Handler::BusConnect();
            }
            else
            {
                AZLOG_ERROR("ArenaAudit: could not generate an audit key; audits disabled for this entity");
            }
            return;
        }
#endif
        if (IsNetEntityRoleAutonomous())
        {
            StartingPointInput::InputEventNotificationBus::MultiHandler::BusConnect(AuditMoveForwardEventId);
            StartingPointInput::InputEventNotificationBus::MultiHandler::BusConnect(AuditMoveRightEventId);
            StartingPointInput::InputEventNotificationBus::MultiHandler::BusConnect(AuditLookXEventId);
            StartingPointInput::InputEventNotificationBus::MultiHandler::BusConnect(AuditLookYEventId);
            StartingPointInput::InputEventNotificationBus::MultiHandler::BusConnect(AuditShootEventId);
        }
    }

    void NetworkArenaAuditComponentController::OnDeactivate([[maybe_unused]] Multiplayer::EntityIsMigrating entityIsMigrating)
    {
        AZ::TickBus::Handler::BusDisconnect();
        StartingPointInput::InputEventNotificationBus::MultiHandler::BusDisconnect();
    }

    void NetworkArenaAuditComponentController::OnPressed(float value)
    {
        FoldInput(value);
    }

    void NetworkArenaAuditComponentController::OnHeld(float value)
    {
        FoldInput(value);
    }

    void NetworkArenaAuditComponentController::OnReleased(float value)
    {
        FoldInput(value);
    }

    void NetworkArenaAuditComponentController::FoldInput(float value)
    {
        const StartingPointInput::InputEventNotificationId* busId =
            StartingPointInput::InputEventNotificationBus::GetCurrentBusId();
        if (!busId)
        {
            return;
        }
        const AZ::u32 eventCrc = busId->m_actionNameCrc;
        m_inputHash = FnvFold(m_inputHash, &eventCrc, sizeof(eventCrc));
        m_inputHash = FnvFold(m_inputHash, &value, sizeof(value));
    }

    float NetworkArenaAuditComponentController::QueryHealth() const
    {
        if (const auto* health = FindComponent<NetworkArenaHealthComponent>())
        {
            return health->GetHealth();
        }
        return -1.0f;
    }

    NetworkArenaAuditComponentController::AuditTag NetworkArenaAuditComponentController::ComputeTag(
        const AuditKey& key, uint32_t challengeId, AZ::u64 nonce,
        const AZ::Vector3& position, float health, AZ::u64 inputHash)
    {
        // fixed-layout message: id | nonce | pos.xyz | health | inputHash
        uint8_t message[sizeof(uint32_t) + sizeof(AZ::u64) + 4 * sizeof(float) + sizeof(AZ::u64)];
        uint8_t* cursor = message;
        auto append = [&cursor](const void* data, size_t size)
        {
            memcpy(cursor, data, size);
            cursor += size;
        };
        const float pos[3] = { position.GetX(), position.GetY(), position.GetZ() };
        append(&challengeId, sizeof(challengeId));
        append(&nonce, sizeof(nonce));
        append(pos, sizeof(pos));
        append(&health, sizeof(health));
        append(&inputHash, sizeof(inputHash));

        AuditTag tag{};
        unsigned int tagLength = 0;
        unsigned char digest[SHA256_DIGEST_LENGTH];
        HMAC(EVP_sha256(),
            key.data(), int(key.size() * sizeof(AZ::u64)),
            message, sizeof(message),
            digest, &tagLength);
        memcpy(tag.data(), digest, AZStd::min(size_t(tagLength), tag.size() * sizeof(AZ::u64)));
        return tag;
    }

#if AZ_TRAIT_CLIENT
    void NetworkArenaAuditComponentController::HandleSetAuditKey(
        [[maybe_unused]] AzNetworking::IConnection* invokingConnection,
        const uint64_t& keyPart0, const uint64_t& keyPart1,
        const uint64_t& keyPart2, const uint64_t& keyPart3)
    {
        m_key = { keyPart0, keyPart1, keyPart2, keyPart3 };
        m_keySet = true;
    }

    void NetworkArenaAuditComponentController::HandleSendAuditChallenge(
        [[maybe_unused]] AzNetworking::IConnection* invokingConnection,
        const uint32_t& challengeId, const uint64_t& nonce)
    {
        if (!m_keySet)
        {
            return; // key RPC is reliable, so this only happens on out-of-order delivery right after spawn
        }
        AZ::Vector3 position = AZ::Vector3::CreateZero();
        AZ::TransformBus::EventResult(position, GetEntityId(), &AZ::TransformBus::Events::GetWorldTranslation);
        const float health = QueryHealth();
        const AuditTag tag = ComputeTag(m_key, challengeId, nonce, position, health, m_inputHash);
        SendAuditResponse(challengeId, position, health, m_inputHash, tag[0], tag[1], tag[2], tag[3]);
    }
#endif

#if AZ_TRAIT_SERVER
    void NetworkArenaAuditComponentController::HandleSendAuditResponse(
        AzNetworking::IConnection* invokingConnection,
        const uint32_t& challengeId, const AZ::Vector3& position, const float& health,
        const uint64_t& inputHash,
        const uint64_t& tag0, const uint64_t& tag1,
        const uint64_t& tag2, const uint64_t& tag3)
    {
        m_lastConnection = invokingConnection;
        if (m_pendingChallengeId == 0 || challengeId != m_pendingChallengeId)
        {
            RegisterStrike("response to a stale or never-issued challenge");
            return;
        }
        const AZ::u64 nonce = m_pendingNonce;
        m_pendingChallengeId = 0;

        // 1. authenticity: the tag must be a valid HMAC over exactly what the client reported
        const AuditTag expected = ComputeTag(m_key, challengeId, nonce, position, health, inputHash);
        if (!ConstantTimeEquals(expected, AuditTag{ tag0, tag1, tag2, tag3 }))
        {
            RegisterStrike("bad authentication tag");
            return;
        }

        // 2. honesty: the authenticated snapshot must match the authoritative simulation
        AZ::Vector3 authoritativePosition = AZ::Vector3::CreateZero();
        AZ::TransformBus::EventResult(
            authoritativePosition, GetEntityId(), &AZ::TransformBus::Events::GetWorldTranslation);
        if (position.GetDistance(authoritativePosition) > GetPositionTolerance())
        {
            RegisterStrike("reported position diverges from the authoritative simulation");
            return;
        }
        const float authoritativeHealth = QueryHealth();
        if (health >= 0.0f && authoritativeHealth >= 0.0f &&
            AZStd::abs(health - authoritativeHealth) > GetHealthTolerance())
        {
            RegisterStrike("reported health diverges from the authoritative simulation");
            return;
        }
    }

    void NetworkArenaAuditComponentController::IssueChallenge()
    {
        m_pendingChallengeId = m_nextChallengeId++;
        m_pendingNonce = (AZ::u64(m_random.GetRandom()) << 32) | m_random.GetRandom();
        m_pendingElapsed = 0.0f;
        SendAuditChallenge(m_pendingChallengeId, m_pendingNonce);
    }

    void NetworkArenaAuditComponentController::ScheduleNextChallenge()
    {
        const float minInterval = AZStd::max(GetMinChallengeInterval(), 1.0f);
        const float maxInterval = AZStd::max(GetMaxChallengeInterval(), minInterval);
        m_nextChallengeIn = minInterval + m_random.GetRandomFloat() * (maxInterval - minInterval);
    }

    void NetworkArenaAuditComponentController::RegisterStrike(const char* reason)
    {
        ++m_strikes;
        AZLOG_WARN(
            "ArenaAudit: entity %llu audit strike %u/%u: %s",
            aznumeric_cast<unsigned long long>(static_cast<AZ::u64>(GetNetEntityId())),
            m_strikes, GetMaxStrikes(), reason);
        if (m_strikes < GetMaxStrikes() || !GetKickOnFailure())
        {
            return;
        }
        AzNetworking::IConnection* connection = m_lastConnection;
        if (auto* networking = AZ::Interface<AzNetworking::INetworking>::Get())
        {
            if (auto* netInterface = networking->RetrieveNetworkInterface(AZ::Name(Multiplayer::MpNetworkInterfaceName)))
            {
                const Multiplayer::NetBindComponent* netBind = GetNetBindComponent();
                if (AzNetworking::IConnection* owning =
                        netBind ? netInterface->GetConnectionSet().GetConnection(netBind->GetOwningConnectionId()) : nullptr)
                {
                    connection = owning;
                }
            }
        }
        if (connection)
        {
            AZLOG_ERROR(
                "ArenaAudit: disconnecting client of entity %llu after %u audit strikes",
                aznumeric_cast<unsigned long long>(static_cast<AZ::u64>(GetNetEntityId())), m_strikes);
            connection->Disconnect(
                AzNetworking::DisconnectReason::TerminatedByServer, AzNetworking::TerminationEndpoint::Local);
        }
    }
#endif

    void NetworkArenaAuditComponentController::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
#if AZ_TRAIT_SERVER
        if (!IsNetEntityRoleAuthority() || !m_keySet)
        {
            return;
        }
        if (m_pendingChallengeId != 0)
        {
            m_pendingElapsed += deltaTime;
            if (m_pendingElapsed > GetResponseDeadline())
            {
                m_pendingChallengeId = 0;
                RegisterStrike("audit response deadline missed");
                ScheduleNextChallenge();
            }
            return;
        }
        m_nextChallengeIn -= deltaTime;
        if (m_nextChallengeIn <= 0.0f)
        {
            IssueChallenge();
            ScheduleNextChallenge();
        }
#else
        AZ_UNUSED(deltaTime);
#endif
    }
} // namespace ArenaShooterNet
