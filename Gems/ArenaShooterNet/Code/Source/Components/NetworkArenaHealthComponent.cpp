/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include "NetworkArenaHealthComponent.h"

#include <AzCore/Component/TransformBus.h>
#include <AzCore/Interface/Interface.h>

#include <Source/ArenaMatchInterface.h>

namespace ArenaShooterNet
{
    NetworkArenaHealthComponentController::NetworkArenaHealthComponentController(NetworkArenaHealthComponent& parent)
        : NetworkArenaHealthComponentControllerBase(parent)
    {
    }

    void NetworkArenaHealthComponentController::OnActivate([[maybe_unused]] Multiplayer::EntityIsMigrating entityIsMigrating)
    {
        if (IsNetEntityRoleAuthority())
        {
            AZ::TransformBus::EventResult(m_spawnPoint, GetEntityId(), &AZ::TransformBus::Events::GetWorldTranslation);
            SetHealth(GetMaxHealth());
            m_dead = false;
            AZ::GameplayNotificationBus::Handler::BusConnect(
                AZ::GameplayNotificationId(GetEntityId(), AZ_CRC_CE("Damage"), azrtti_typeid<float>()));
        }
    }

    void NetworkArenaHealthComponentController::OnDeactivate([[maybe_unused]] Multiplayer::EntityIsMigrating entityIsMigrating)
    {
        AZ::GameplayNotificationBus::Handler::BusDisconnect();
        AZ::TickBus::Handler::BusDisconnect();
    }

    void NetworkArenaHealthComponentController::OnEventBegin(const AZStd::any& value)
    {
        if (m_dead || !IsNetEntityRoleAuthority())
        {
            return;
        }

        // during warm-up / map-vote downtime damage is disabled
        if (auto* match = AZ::Interface<IArenaMatch>::Get(); match && !match->IsCombatEnabled())
        {
            return;
        }

        float damage = 0.0f;
        AZ::EntityId attacker;
        if (const ArenaDamage* asArena = AZStd::any_cast<ArenaDamage>(&value))
        {
            damage = asArena->m_damage;
            attacker = asArena->m_attacker;
        }
        else if (const float* asFloat = AZStd::any_cast<float>(&value))
        {
            damage = *asFloat;
        }
        else if (const double* asDouble = AZStd::any_cast<double>(&value))
        {
            damage = static_cast<float>(*asDouble);
        }
        if (damage <= 0.0f)
        {
            return;
        }

        const float newHealth = AZStd::max(GetHealth() - damage, 0.0f);
        SetHealth(newHealth);
        if (newHealth <= 0.0f)
        {
            if (auto* match = AZ::Interface<IArenaMatch>::Get())
            {
                match->ReportKill(attacker, GetEntityId());
            }
            Die();
        }
    }

    void NetworkArenaHealthComponentController::Die()
    {
        m_dead = true;
        SetIsDead(true); // clients play the death animation off this flag
        m_respawnTimer = GetRespawnDelay();
        // the body stays in place while dead so the death animation is visible;
        // it takes no damage (m_dead) and respawns at the spawn point below
        AZ::TickBus::Handler::BusConnect();
    }

    void NetworkArenaHealthComponentController::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        m_respawnTimer -= deltaTime;
        if (m_respawnTimer <= 0.0f)
        {
            AZ::TickBus::Handler::BusDisconnect();
            Respawn();
        }
    }

    void NetworkArenaHealthComponentController::Respawn()
    {
        AZ::TransformBus::Event(GetEntityId(), &AZ::TransformBus::Events::SetWorldTranslation, m_spawnPoint);
        SetHealth(GetMaxHealth());
        SetIsDead(false);
        m_dead = false;
    }
} // namespace ArenaShooterNet
