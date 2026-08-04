/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <Source/AutoGen/NetworkArenaPlayerComponent.AutoComponent.h>

#include <StartingPointInput/InputEventNotificationBus.h>

namespace ArenaShooterNet
{
    //! Server-authoritative, client-predicted arena-shooter player.
    //! On the autonomous client it samples the ArenaShooter input events
    //! (MoveForward/MoveRight/LookX/LookY/Shoot from arenashooter.inputbindings)
    //! into network inputs; movement is processed through the networked
    //! character controller on both the predicting client and the server, and
    //! shots are validated and resolved exclusively on the server (fire-rate
    //! cap + server-side raycast) — clients never report hits.
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
        // StartingPointInput::InputEventNotificationBus
        void OnPressed(float value) override;
        void OnHeld(float value) override;
        void OnReleased(float value) override;

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
    };
} // namespace ArenaShooterNet
