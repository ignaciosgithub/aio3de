/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <Source/AutoGen/NetworkArenaHealthComponent.AutoComponent.h>

#include <AzCore/Component/TickBus.h>
#include <AzCore/Math/Vector3.h>
#include <LmbrCentral/Scripting/GameplayNotificationBus.h>

namespace ArenaShooterNet
{
    //! Server-authoritative health. The authority listens for server-local
    //! "Damage" gameplay events (sent by NetworkArenaPlayerComponent's
    //! server-side shot resolution), applies them, and replicates the Health
    //! network property to every client - clients can never set their own
    //! health. Death teleports the entity to its spawn point after a delay
    //! and restores full health.
    class NetworkArenaHealthComponentController
        : public NetworkArenaHealthComponentControllerBase
        , private AZ::GameplayNotificationBus::Handler
        , private AZ::TickBus::Handler
    {
    public:
        explicit NetworkArenaHealthComponentController(NetworkArenaHealthComponent& parent);

        // NetworkArenaHealthComponentControllerBase
        void OnActivate(Multiplayer::EntityIsMigrating entityIsMigrating) override;
        void OnDeactivate(Multiplayer::EntityIsMigrating entityIsMigrating) override;

    private:
        // AZ::GameplayNotificationBus (server only)
        void OnEventBegin(const AZStd::any& value) override;

        // AZ::TickBus (server only, while dead)
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;

        void Die();
        void Respawn();

        AZ::Vector3 m_spawnPoint = AZ::Vector3::CreateZero();
        float m_respawnTimer = 0.0f;
        bool m_dead = false;
    };
} // namespace ArenaShooterNet
