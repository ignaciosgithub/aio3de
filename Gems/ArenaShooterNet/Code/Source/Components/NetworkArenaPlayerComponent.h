/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <Source/AutoGen/NetworkArenaPlayerComponent.AutoComponent.h>

#include <AzCore/std/containers/fixed_vector.h>
#include <AzCore/std/string/string.h>
#include <StartingPointInput/InputEventNotificationBus.h>

namespace ArenaShooterNet
{
    //! Server-authoritative, client-predicted arena-shooter player.
    //! On the autonomous client it samples the ArenaShooter input events
    //! (MoveForward/MoveRight/LookX/LookY/Shoot from arenashooter.inputbindings)
    //! into network inputs; movement is processed through the networked
    //! character controller on both the predicting client and the server, and
    //! shots are validated and resolved exclusively on the server (fire-rate
    //! cap + server-side raycast) - clients never report hits.
    class NetworkArenaPlayerComponentController
        : public NetworkArenaPlayerComponentControllerBase
        , private StartingPointInput::InputEventNotificationBus::MultiHandler
    {
    public:
        explicit NetworkArenaPlayerComponentController(NetworkArenaPlayerComponent& parent);

        // NetworkArenaPlayerComponentControllerBase
        void OnActivate(Multiplayer::EntityIsMigrating entityIsMigrating) override;
        void OnDeactivate(Multiplayer::EntityIsMigrating entityIsMigrating) override;

        // MultiplayerController
        Multiplayer::MultiplayerController::InputPriorityOrder GetInputOrder() const override;
        void CreateInput(Multiplayer::NetworkInput& input, float deltaTime) override;
        void ProcessInput(Multiplayer::NetworkInput& input, float deltaTime) override;

        //! Authority-side map-vote relay to the match controller.
        void HandleSendMapVote(AzNetworking::IConnection* invokingConnection, const uint8_t& mapIndex) override;

    private:
        //! One server-side weapon configuration parsed from WeaponConfigs.
        struct Weapon
        {
            AZStd::string m_name;
            float m_damage = 10.0f;
            float m_interval = 0.2f;
            float m_range = 200.0f;
        };
        static constexpr size_t MaxWeapons = 8;

        // StartingPointInput::InputEventNotificationBus
        void OnPressed(float value) override;
        void OnHeld(float value) override;
        void OnReleased(float value) override;

        void ParseWeaponConfigs();
        void SelectWeapon(int index); // client-side, wraps/clamps
        const Weapon& CurrentServerWeapon() const;

        void ServerShoot();

        // client-side sampled input state
        float m_forward = 0.0f;
        float m_strafe = 0.0f;
        float m_yawDegrees = 0.0f;
        float m_pitchDegrees = 0.0f;
        bool m_shooting = false;
        bool m_yawInitialized = false;

        // server-side fire-rate enforcement
        float m_fireCooldown = 0.0f;

        // weapon loadout (parsed identically on every role from WeaponConfigs)
        AZStd::fixed_vector<Weapon, MaxWeapons> m_weapons;
        uint8_t m_localWeaponIndex = 0; // client-selected slot, sent as input
    };
} // namespace ArenaShooterNet
