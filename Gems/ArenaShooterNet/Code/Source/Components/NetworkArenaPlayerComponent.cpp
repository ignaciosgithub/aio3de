/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include "NetworkArenaPlayerComponent.h"

#include <AzCore/Component/TransformBus.h>
#include <AzCore/Math/Quaternion.h>
#include <AzFramework/Physics/Common/PhysicsSceneQueries.h>
#include <AzFramework/Physics/PhysicsScene.h>
#include <AzFramework/Physics/PhysicsSystem.h>
#include <LmbrCentral/Scripting/GameplayNotificationBus.h>
#include <AzCore/StringFunc/StringFunc.h>
#include <Multiplayer/Components/NetworkCharacterComponent.h>

#include <Source/ArenaMatchInterface.h>

namespace ArenaShooterNet
{
    namespace
    {
        const StartingPointInput::InputEventNotificationId MoveForwardEventId("MoveForward");
        const StartingPointInput::InputEventNotificationId MoveRightEventId("MoveRight");
        const StartingPointInput::InputEventNotificationId LookXEventId("LookX");
        const StartingPointInput::InputEventNotificationId LookYEventId("LookY");
        const StartingPointInput::InputEventNotificationId ShootEventId("Shoot");
        const StartingPointInput::InputEventNotificationId Vote1EventId("Vote1");
        const StartingPointInput::InputEventNotificationId Vote2EventId("Vote2");
        const StartingPointInput::InputEventNotificationId Vote3EventId("Vote3");
        const StartingPointInput::InputEventNotificationId Vote4EventId("Vote4");
        const StartingPointInput::InputEventNotificationId WeaponScrollEventId("WeaponScroll");
        const StartingPointInput::InputEventNotificationId WeaponNextEventId("WeaponNext");
        const StartingPointInput::InputEventNotificationId WeaponPrevEventId("WeaponPrev");
    } // namespace

    NetworkArenaPlayerComponentController::NetworkArenaPlayerComponentController(NetworkArenaPlayerComponent& parent)
        : NetworkArenaPlayerComponentControllerBase(parent)
    {
    }

    void NetworkArenaPlayerComponentController::OnActivate([[maybe_unused]] Multiplayer::EntityIsMigrating entityIsMigrating)
    {
        ParseWeaponConfigs();
        if (IsNetEntityRoleAutonomous())
        {
            StartingPointInput::InputEventNotificationBus::MultiHandler::BusConnect(MoveForwardEventId);
            StartingPointInput::InputEventNotificationBus::MultiHandler::BusConnect(MoveRightEventId);
            StartingPointInput::InputEventNotificationBus::MultiHandler::BusConnect(LookXEventId);
            StartingPointInput::InputEventNotificationBus::MultiHandler::BusConnect(LookYEventId);
            StartingPointInput::InputEventNotificationBus::MultiHandler::BusConnect(ShootEventId);
            StartingPointInput::InputEventNotificationBus::MultiHandler::BusConnect(Vote1EventId);
            StartingPointInput::InputEventNotificationBus::MultiHandler::BusConnect(Vote2EventId);
            StartingPointInput::InputEventNotificationBus::MultiHandler::BusConnect(Vote3EventId);
            StartingPointInput::InputEventNotificationBus::MultiHandler::BusConnect(Vote4EventId);
            StartingPointInput::InputEventNotificationBus::MultiHandler::BusConnect(WeaponScrollEventId);
            StartingPointInput::InputEventNotificationBus::MultiHandler::BusConnect(WeaponNextEventId);
            StartingPointInput::InputEventNotificationBus::MultiHandler::BusConnect(WeaponPrevEventId);
        }
    }

    void NetworkArenaPlayerComponentController::OnDeactivate([[maybe_unused]] Multiplayer::EntityIsMigrating entityIsMigrating)
    {
        StartingPointInput::InputEventNotificationBus::MultiHandler::BusDisconnect();
    }

    Multiplayer::MultiplayerController::InputPriorityOrder NetworkArenaPlayerComponentController::GetInputOrder() const
    {
        return Multiplayer::MultiplayerController::InputPriorityOrder::Default;
    }

    void NetworkArenaPlayerComponentController::OnPressed(float value)
    {
        const StartingPointInput::InputEventNotificationId* busId =
            StartingPointInput::InputEventNotificationBus::GetCurrentBusId();
        if (!busId)
        {
            return;
        }
        if (*busId == MoveForwardEventId)
        {
            m_forward = value;
        }
        else if (*busId == MoveRightEventId)
        {
            m_strafe = value;
        }
        else if (*busId == LookXEventId)
        {
            m_yawDegrees -= value;
        }
        else if (*busId == LookYEventId)
        {
            m_pitchDegrees = AZ::GetClamp(m_pitchDegrees - value, -85.0f, 85.0f);
        }
        else if (*busId == ShootEventId)
        {
            m_shooting = true;
        }
        else if (*busId == Vote1EventId)
        {
            SendMapVote(0);
        }
        else if (*busId == Vote2EventId)
        {
            SendMapVote(1);
        }
        else if (*busId == Vote3EventId)
        {
            SendMapVote(2);
        }
        else if (*busId == Vote4EventId)
        {
            SendMapVote(3);
        }
        else if (*busId == WeaponScrollEventId)
        {
            // mouse wheel: positive delta = next weapon, negative = previous
            if (value > 0.0f)
            {
                SelectWeapon(m_localWeaponIndex + 1);
            }
            else if (value < 0.0f)
            {
                SelectWeapon(int(m_localWeaponIndex) - 1);
            }
        }
        else if (*busId == WeaponNextEventId)
        {
            SelectWeapon(m_localWeaponIndex + 1);
        }
        else if (*busId == WeaponPrevEventId)
        {
            SelectWeapon(int(m_localWeaponIndex) - 1);
        }
    }

    void NetworkArenaPlayerComponentController::OnHeld(float value)
    {
        const StartingPointInput::InputEventNotificationId* busId =
            StartingPointInput::InputEventNotificationBus::GetCurrentBusId();
        if (busId &&
            (*busId == Vote1EventId || *busId == Vote2EventId || *busId == Vote3EventId || *busId == Vote4EventId ||
             *busId == WeaponNextEventId || *busId == WeaponPrevEventId || *busId == WeaponScrollEventId))
        {
            return; // votes and weapon switches fire once on press, never repeat while held
        }
        OnPressed(value);
    }

    void NetworkArenaPlayerComponentController::OnReleased(float value)
    {
        const StartingPointInput::InputEventNotificationId* busId =
            StartingPointInput::InputEventNotificationBus::GetCurrentBusId();
        if (!busId)
        {
            return;
        }
        if (*busId == MoveForwardEventId)
        {
            m_forward = 0.0f;
        }
        else if (*busId == MoveRightEventId)
        {
            m_strafe = 0.0f;
        }
        else if (*busId == ShootEventId)
        {
            m_shooting = false;
        }
        AZ_UNUSED(value);
    }

    void NetworkArenaPlayerComponentController::CreateInput(Multiplayer::NetworkInput& input, [[maybe_unused]] float deltaTime)
    {
        if (!m_yawInitialized)
        {
            AZ::Transform tm = AZ::Transform::CreateIdentity();
            AZ::TransformBus::EventResult(tm, GetEntityId(), &AZ::TransformBus::Events::GetWorldTM);
            m_yawDegrees = AZ::RadToDeg(tm.GetRotation().GetEulerRadians().GetZ());
            m_yawInitialized = true;
        }

        auto* playerInput = input.FindComponentInput<NetworkArenaPlayerComponentNetworkInput>();
        if (!playerInput)
        {
            return;
        }
        playerInput->m_fwdBack = m_forward;
        playerInput->m_leftRight = m_strafe;
        playerInput->m_viewYaw = m_yawDegrees;
        playerInput->m_viewPitch = m_pitchDegrees;
        playerInput->m_shoot = m_shooting ? 1.0f : 0.0f;
        playerInput->m_weaponIndex = m_localWeaponIndex;
    }

    void NetworkArenaPlayerComponentController::ProcessInput(Multiplayer::NetworkInput& input, float deltaTime)
    {
        auto* playerInput = input.FindComponentInput<NetworkArenaPlayerComponentNetworkInput>();
        if (!playerInput)
        {
            return;
        }

        // view rotation (yaw on the entity; pitch is only used for the shot direction)
        const AZ::Quaternion yawRot = AZ::Quaternion::CreateRotationZ(AZ::DegToRad(playerInput->m_viewYaw));
        AZ::TransformBus::Event(GetEntityId(), &AZ::TransformBus::Events::SetWorldRotationQuaternion, yawRot);

        // movement through the networked character controller (predicted on the
        // autonomous client, authoritative on the server)
        AZ::Vector3 moveLocal(
            AZ::GetClamp(playerInput->m_leftRight, -1.0f, 1.0f),
            AZ::GetClamp(playerInput->m_fwdBack, -1.0f, 1.0f),
            0.0f);
        if (moveLocal.GetLengthSq() > 1.0f)
        {
            moveLocal.Normalize();
        }
        const AZ::Vector3 velocity = yawRot.TransformVector(moveLocal) * GetMoveSpeed();
        GetNetworkCharacterComponentController()->TryMoveWithVelocity(velocity, deltaTime);

        // shooting is resolved only on the server: clients just transmit the
        // trigger state, never a hit result
        if (IsNetEntityRoleAuthority())
        {
            // weapon selection: the client only picks a slot; damage/interval/
            // range always come from the server-side config for that slot
            const uint8_t requested = playerInput->m_weaponIndex;
            if (!m_weapons.empty())
            {
                SetActiveWeapon(AZStd::min(requested, uint8_t(m_weapons.size() - 1)));
            }

            m_fireCooldown = AZStd::max(m_fireCooldown - deltaTime, 0.0f);
            if (playerInput->m_shoot > 0.5f && m_fireCooldown <= 0.0f)
            {
                m_fireCooldown = CurrentServerWeapon().m_interval; // server-enforced fire rate
                m_pitchDegrees = playerInput->m_viewPitch;
                m_yawDegrees = playerInput->m_viewYaw;
                ServerShoot();
            }
        }
    }

    void NetworkArenaPlayerComponentController::ParseWeaponConfigs()
    {
        m_weapons.clear();
        AZStd::vector<AZStd::string> entries;
        AZ::StringFunc::Tokenize(GetWeaponConfigs().c_str(), entries, '|');
        for (const AZStd::string& entry : entries)
        {
            if (m_weapons.size() >= MaxWeapons)
            {
                break;
            }
            AZStd::vector<AZStd::string> fields;
            AZ::StringFunc::Tokenize(entry.c_str(), fields, ',');
            if (fields.size() != 4)
            {
                continue;
            }
            Weapon weapon;
            weapon.m_name = fields[0];
            AZ::StringFunc::Strip(weapon.m_name, " \t");
            weapon.m_damage = AZStd::max(float(atof(fields[1].c_str())), 0.0f);
            weapon.m_interval = AZStd::max(float(atof(fields[2].c_str())), 0.01f);
            weapon.m_range = AZStd::max(float(atof(fields[3].c_str())), 1.0f);
            m_weapons.push_back(weapon);
        }
        if (m_weapons.empty())
        {
            // legacy single-weapon setup from the individual properties
            m_weapons.push_back(Weapon{ "Default", GetFireDamage(), GetFireInterval(), GetFireRange() });
        }
    }

    void NetworkArenaPlayerComponentController::SelectWeapon(int index)
    {
        const int count = int(m_weapons.size());
        if (count <= 1)
        {
            return;
        }
        m_localWeaponIndex = uint8_t(((index % count) + count) % count); // wrap both directions
    }

    const NetworkArenaPlayerComponentController::Weapon& NetworkArenaPlayerComponentController::CurrentServerWeapon() const
    {
        const size_t index = AZStd::min(size_t(GetActiveWeapon()), m_weapons.size() - 1);
        return m_weapons[index];
    }

    void NetworkArenaPlayerComponentController::HandleSendMapVote(
        [[maybe_unused]] AzNetworking::IConnection* invokingConnection, const uint8_t& mapIndex)
    {
        if (auto* match = AZ::Interface<IArenaMatch>::Get())
        {
            match->SubmitMapVote(GetEntityId(), mapIndex);
        }
    }

    void NetworkArenaPlayerComponentController::ServerShoot()
    {
        auto* physicsSystem = AZ::Interface<AzPhysics::SystemInterface>::Get();
        if (!physicsSystem)
        {
            return;
        }
        AzPhysics::Scene* scene =
            physicsSystem->GetScene(physicsSystem->GetSceneHandle(AzPhysics::DefaultPhysicsSceneName));
        if (!scene)
        {
            return;
        }

        AZ::Vector3 selfPos = AZ::Vector3::CreateZero();
        AZ::TransformBus::EventResult(selfPos, GetEntityId(), &AZ::TransformBus::Events::GetWorldTranslation);
        const AZ::Vector3 eye = selfPos + AZ::Vector3(0.0f, 0.0f, GetEyeHeight());

        const AZ::Quaternion aimRot = AZ::Quaternion::CreateRotationZ(AZ::DegToRad(m_yawDegrees)) *
            AZ::Quaternion::CreateRotationX(AZ::DegToRad(m_pitchDegrees));
        const AZ::Vector3 direction = aimRot.TransformVector(AZ::Vector3::CreateAxisY());

        AzPhysics::RayCastRequest request;
        request.m_start = eye;
        request.m_direction = direction;
        request.m_distance = CurrentServerWeapon().m_range;
        request.m_reportMultipleHits = true;

        AzPhysics::SceneQueryHits hits = scene->QueryScene(&request);
        const AzPhysics::SceneQueryHit* closest = nullptr;
        for (const AzPhysics::SceneQueryHit& hit : hits.m_hits)
        {
            if (hit.m_entityId == GetEntityId())
            {
                continue;
            }
            if (!closest || hit.m_distance < closest->m_distance)
            {
                closest = &hit;
            }
        }

        if (closest && closest->m_entityId.IsValid())
        {
            // server-local damage event: NetworkArenaHealthComponent's authority
            // controller handles it and replicates the resulting health
            AZ::GameplayNotificationBus::Event(
                AZ::GameplayNotificationId(closest->m_entityId, AZ_CRC_CE("Damage"), azrtti_typeid<float>()),
                &AZ::GameplayNotificationBus::Events::OnEventBegin,
                AZStd::any(ArenaDamage{ CurrentServerWeapon().m_damage, GetEntityId() }));
        }
    }
} // namespace ArenaShooterNet
