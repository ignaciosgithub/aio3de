/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include "NetworkArenaAuditComponent.h"

#include <AntiTamper/ArenaAttestDataset.h>
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
                if (GetPowRequired())
                {
                    m_powParams.m_seed = (AZ::u64(m_random.GetRandom()) << 32) | m_random.GetRandom();
                    m_powParams.m_memoryKib = GetPowMemoryKib();
                    m_powParams.m_passes = GetPowPasses();
                    m_powParams.m_difficultyBits = GetPowDifficultyBits();
                    m_powPending = true;
                    m_powElapsed = 0.0f;
                    SetPowChallenge(
                        m_powParams.m_seed, m_powParams.m_memoryKib,
                        m_powParams.m_passes, m_powParams.m_difficultyBits);
                }
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
        m_powCancel = true;
        if (m_powThread.joinable())
        {
            m_powThread.join();
        }
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
        const AZ::Vector3& position, float health, AZ::u64 inputHash,
        const AZ::Attestation::Digest& attestDigest)
    {
        // fixed-layout message: id | nonce | pos.xyz | health | inputHash | attestDigest
        uint8_t message[sizeof(uint32_t) + sizeof(AZ::u64) + 4 * sizeof(float) + sizeof(AZ::u64) +
            sizeof(AZ::Attestation::Digest)];
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
        append(attestDigest.data(), sizeof(AZ::Attestation::Digest));

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
        const uint32_t& challengeId, const uint64_t& nonce,
        const uint64_t& attestSeed, const uint32_t& attestOpCount)
    {
        if (!m_keySet)
        {
            return; // key RPC is reliable, so this only happens on out-of-order delivery right after spawn
        }
        AZ::Vector3 position = AZ::Vector3::CreateZero();
        AZ::TransformBus::EventResult(position, GetEntityId(), &AZ::TransformBus::Events::GetWorldTranslation);
        const float health = QueryHealth();
        AZ::Attestation::Digest attestDigest{};
        if (attestOpCount > 0)
        {
            const auto& dataset = ArenaAttestDataset::Get();
            attestDigest = AZ::Attestation::ExecuteProgram(attestSeed, attestOpCount, dataset.data(), dataset.size());
        }
        const AuditTag tag = ComputeTag(m_key, challengeId, nonce, position, health, m_inputHash, attestDigest);
        SendAuditResponse(
            challengeId, position, health, m_inputHash,
            attestDigest[0], attestDigest[1], attestDigest[2], attestDigest[3],
            tag[0], tag[1], tag[2], tag[3]);
    }

    void NetworkArenaAuditComponentController::HandleSetPowChallenge(
        [[maybe_unused]] AzNetworking::IConnection* invokingConnection,
        const uint64_t& seed, const uint32_t& memoryKib,
        const uint32_t& passes, const uint32_t& difficultyBits)
    {
        if (m_powSolving)
        {
            return; // one challenge per session
        }
        AZ::Attestation::PowParams params;
        params.m_seed = seed;
        // clamp to sane bounds so a hostile server cannot demand absurd work
        params.m_memoryKib = AZStd::min(memoryKib, 65536u);
        params.m_passes = AZStd::min(passes, 16u);
        params.m_difficultyBits = AZStd::min(difficultyBits, 24u);
        m_powSolving = true;
        m_powDone = false;
        m_powCancel = false;
        // solve on a background thread so gameplay frames never stall
        m_powThread = AZStd::thread(
            [this, params]()
            {
                for (AZ::u64 nonce = 0; !m_powCancel; ++nonce)
                {
                    if (AZ::Attestation::VerifyPow(params, nonce))
                    {
                        m_powNonce = nonce;
                        m_powDone = true;
                        return;
                    }
                }
            });
        AZ::TickBus::Handler::BusConnect();
    }
#endif

#if AZ_TRAIT_SERVER
    void NetworkArenaAuditComponentController::HandleSendAuditResponse(
        AzNetworking::IConnection* invokingConnection,
        const uint32_t& challengeId, const AZ::Vector3& position, const float& health,
        const uint64_t& inputHash,
        const uint64_t& attest0, const uint64_t& attest1,
        const uint64_t& attest2, const uint64_t& attest3,
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

        const AZ::Attestation::Digest attestDigest{ attest0, attest1, attest2, attest3 };

        // 1. authenticity: the tag must be a valid HMAC over exactly what the client reported
        const AuditTag expected = ComputeTag(m_key, challengeId, nonce, position, health, inputHash, attestDigest);
        if (!ConstantTimeEquals(expected, AuditTag{ tag0, tag1, tag2, tag3 }))
        {
            RegisterStrike("bad authentication tag");
            return;
        }

        // 2. attestation: the client must have executed this challenge's random
        // program over the same reference dataset
        if (m_pendingAttestOpCount > 0 && !ConstantTimeEquals(m_pendingAttestExpected, attestDigest))
        {
            RegisterStrike("attestation digest mismatch (tampered dataset or emulated client)");
            return;
        }

        // 3. honesty: the authenticated snapshot must match the authoritative simulation
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
        ++m_challengesIssued;

        m_pendingAttestSeed = 0;
        m_pendingAttestOpCount = 0;
        const AZ::u32 attestEvery = GetAttestEveryNChallenges();
        if (attestEvery > 0 && (m_challengesIssued % attestEvery) == 0)
        {
            m_pendingAttestSeed = (AZ::u64(m_random.GetRandom()) << 32) | m_random.GetRandom();
            m_pendingAttestOpCount = AZStd::max(GetAttestOpCount(), 64u);
            const auto& dataset = ArenaAttestDataset::Get();
            m_pendingAttestExpected = AZ::Attestation::ExecuteProgram(
                m_pendingAttestSeed, m_pendingAttestOpCount, dataset.data(), dataset.size());
        }
        SendAuditChallenge(m_pendingChallengeId, m_pendingNonce, m_pendingAttestSeed, m_pendingAttestOpCount);
    }

    void NetworkArenaAuditComponentController::HandleSendPowSolution(
        AzNetworking::IConnection* invokingConnection, const uint64_t& nonce)
    {
        m_lastConnection = invokingConnection;
        if (!m_powPending)
        {
            return; // already verified or never required
        }
        m_powPending = false;
        if (AZ::Attestation::VerifyPow(m_powParams, nonce))
        {
            m_powVerified = true;
        }
        else
        {
            RegisterStrike("invalid proof-of-work solution");
        }
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
#if AZ_TRAIT_CLIENT
        // autonomous: deliver the background proof-of-work solution when ready
        if (IsNetEntityRoleAutonomous())
        {
            if (m_powSolving && m_powDone)
            {
                m_powSolving = false;
                if (m_powThread.joinable())
                {
                    m_powThread.join();
                }
                SendPowSolution(m_powNonce);
                if (!IsNetEntityRoleAuthority())
                {
                    AZ::TickBus::Handler::BusDisconnect();
                }
            }
            if (!IsNetEntityRoleAuthority())
            {
                return;
            }
        }
#endif
#if AZ_TRAIT_SERVER
        if (!IsNetEntityRoleAuthority() || !m_keySet)
        {
            return;
        }
        if (m_powPending)
        {
            m_powElapsed += deltaTime;
            if (m_powElapsed > GetPowDeadline())
            {
                m_powPending = false;
                RegisterStrike("proof-of-work deadline missed");
            }
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
