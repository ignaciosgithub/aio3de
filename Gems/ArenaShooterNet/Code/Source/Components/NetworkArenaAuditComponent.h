/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <Source/AutoGen/NetworkArenaAuditComponent.AutoComponent.h>

#include <AzCore/Component/TickBus.h>
#include <AzCore/Math/Attestation.h>
#include <AzCore/Math/Random.h>
#include <AzCore/std/containers/array.h>
#include <AzCore/std/parallel/atomic.h>
#include <AzCore/std/parallel/thread.h>

#include <StartingPointInput/InputEventNotificationBus.h>

namespace ArenaShooterNet
{
    //! Detection-side anti-tamper: unpredictable audit challenges.
    //!
    //! The authority (server) periodically sends the autonomous client a random
    //! nonce; the client must answer within a deadline with an HMAC-SHA256
    //! authenticated snapshot (nonce, position, health, rolling hash of its raw
    //! local inputs) keyed with a per-session audit key. The server verifies the
    //! tag and checks the reported state against its own authoritative
    //! simulation; failed, missed, or mismatching audits accumulate strikes and
    //! can disconnect the client.
    //!
    //! This layer *detects* hooked or dishonest clients - it cannot *prevent*
    //! them (a sophisticated cheat can keep a clean shadow state just for
    //! audits). Prevention remains the server-authoritative simulation
    //! (NetworkArenaPlayerComponent/NetworkArenaHealthComponent); transport
    //! integrity (per-packet auth tags, sequence numbers, replay window, key
    //! rotation) comes from enabling DTLS (net_UdpUseEncryption).
    //!
    //! Two additional layers harden the audits:
    //! - Randomized-program attestation (RandomX-style): every Nth challenge
    //!   carries a random seed; both sides execute the same seed-derived
    //!   program over the shared ArenaAttestDataset and the digests must
    //!   match. Programs differ per challenge, so answers cannot be
    //!   precomputed; a client with tampered dataset content fails.
    //! - Memory-hard proof-of-work at spawn: the client must solve a
    //!   Hashcash-style search over a memory-hard function before the
    //!   deadline, making throwaway accounts and kick-reconnect loops
    //!   costly. Verification on the server is a single evaluation.
    //!
    //! All audit RPCs are reliable, so packet loss delays but never drops a
    //! challenge or response.
    class NetworkArenaAuditComponentController
        : public NetworkArenaAuditComponentControllerBase
        , private AZ::TickBus::Handler
        , private StartingPointInput::InputEventNotificationBus::MultiHandler
    {
    public:
        using AuditKey = AZStd::array<AZ::u64, 4>;
        using AuditTag = AZStd::array<AZ::u64, 4>;

        explicit NetworkArenaAuditComponentController(NetworkArenaAuditComponent& parent);

        // NetworkArenaAuditComponentControllerBase
        void OnActivate(Multiplayer::EntityIsMigrating entityIsMigrating) override;
        void OnDeactivate(Multiplayer::EntityIsMigrating entityIsMigrating) override;

#if AZ_TRAIT_CLIENT
        void HandleSetAuditKey(
            AzNetworking::IConnection* invokingConnection,
            const uint64_t& keyPart0, const uint64_t& keyPart1,
            const uint64_t& keyPart2, const uint64_t& keyPart3) override;
        void HandleSendAuditChallenge(
            AzNetworking::IConnection* invokingConnection,
            const uint32_t& challengeId, const uint64_t& nonce,
            const uint64_t& attestSeed, const uint32_t& attestOpCount) override;
        void HandleSetPowChallenge(
            AzNetworking::IConnection* invokingConnection,
            const uint64_t& seed, const uint32_t& memoryKib,
            const uint32_t& passes, const uint32_t& difficultyBits) override;
#endif

#if AZ_TRAIT_SERVER
        void HandleSendAuditResponse(
            AzNetworking::IConnection* invokingConnection,
            const uint32_t& challengeId, const AZ::Vector3& position, const float& health,
            const uint64_t& inputHash,
            const uint64_t& attest0, const uint64_t& attest1,
            const uint64_t& attest2, const uint64_t& attest3,
            const uint64_t& tag0, const uint64_t& tag1,
            const uint64_t& tag2, const uint64_t& tag3) override;
        void HandleSendPowSolution(
            AzNetworking::IConnection* invokingConnection, const uint64_t& nonce) override;
#endif

    private:
        // AZ::TickBus (authority only: challenge scheduling + deadline enforcement)
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;

        // StartingPointInput::InputEventNotificationBus (autonomous only: rolling input hash)
        void OnPressed(float value) override;
        void OnHeld(float value) override;
        void OnReleased(float value) override;

        void FoldInput(float value);
        float QueryHealth() const;
        static AuditTag ComputeTag(
            const AuditKey& key, uint32_t challengeId, AZ::u64 nonce,
            const AZ::Vector3& position, float health, AZ::u64 inputHash,
            const AZ::Attestation::Digest& attestDigest);

#if AZ_TRAIT_SERVER
        void IssueChallenge();
        void RegisterStrike(const char* reason);
        void ScheduleNextChallenge();
#endif

        AuditKey m_key{};
        bool m_keySet = false;

        // autonomous: background proof-of-work solver
        AZStd::thread m_powThread;
        AZStd::atomic_bool m_powDone{ false };
        AZStd::atomic_bool m_powCancel{ false };
        AZStd::atomic<AZ::u64> m_powNonce{ 0 };
        bool m_powSolving = false;

        // autonomous: FNV-1a rolling hash of every raw local input event
        AZ::u64 m_inputHash = 14695981039346656037ull;

        // authority: pending challenge bookkeeping
        AZ::SimpleLcgRandom m_random;
        uint32_t m_nextChallengeId = 1;
        uint32_t m_pendingChallengeId = 0; //!< 0 = none pending
        AZ::u64 m_pendingNonce = 0;
        AZ::u64 m_pendingAttestSeed = 0;
        AZ::u32 m_pendingAttestOpCount = 0;
        AZ::Attestation::Digest m_pendingAttestExpected{};
        uint32_t m_challengesIssued = 0;
        float m_pendingElapsed = 0.0f;

        // authority: proof-of-work bookkeeping
        AZ::Attestation::PowParams m_powParams;
        bool m_powPending = false;
        bool m_powVerified = false;
        float m_powElapsed = 0.0f;
        float m_nextChallengeIn = 0.0f;
        uint32_t m_strikes = 0;
        AzNetworking::IConnection* m_lastConnection = nullptr;
    };
} // namespace ArenaShooterNet
