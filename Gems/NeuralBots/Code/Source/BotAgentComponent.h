/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Math/Random.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/std/containers/deque.h>

#include "MlpPolicy.h"

namespace NeuralBots
{
    //! Human-behavior limits applied to a bot so it competes like a player,
    //! not like an aimbot.
    struct HumanConstraints
    {
        AZ_TYPE_INFO(HumanConstraints, "{7C1FBA5E-3C2E-4C0F-9A2B-6C7D1E4F5A60}");

        static void Reflect(AZ::ReflectContext* context);

        float m_reactionTime = 0.20f;     //!< seconds between the world changing and the bot perceiving it
        float m_aimErrorDegrees = 2.5f;   //!< std-dev of Gaussian aim noise per shot
        float m_maxTurnDegPerSec = 360.0f; //!< max look rotation speed
        float m_maxShotsPerSecond = 5.0f; //!< fire-rate cap
    };

    //! The observation the policy sees and the action it produces. The layout
    //! doubles as the training-data contract for AIBackbone models.
    namespace Policy
    {
        // inputs: [toTargetLocalX, toTargetLocalY, toTargetLocalZ (normalized dir),
        //          distance/50, lineOfSight (0/1), ownSpeed/10,
        //          cosAngleToTarget, timeSinceSeen/2]
        constexpr size_t InputWidth = 8;
        // outputs: [moveLocalX (-1..1), moveLocalY (-1..1),
        //           turn (-1..1 of max turn speed), shoot (>0.5), jump (>0.5)]
        constexpr size_t OutputWidth = 5;
    } // namespace Policy

    //! Neural-net-driven combatant. Perceives a target entity, evaluates a
    //! policy (an AIBackbone-exported .weights.json MLP, or a built-in
    //! chase/strafe/shoot heuristic when no model is assigned), and acts
    //! through the same channels a player uses: character-controller movement,
    //! yaw rotation, and hitscan shots that send "Damage" gameplay events
    //! (compatible with the ArenaShooter kit's Health.lua).
    class BotAgentComponent
        : public AZ::Component
        , private AZ::TickBus::Handler
    {
    public:
        AZ_COMPONENT(BotAgentComponent, "{A9D31F04-52B7-4D6E-8F1C-2B9E0A7C4D53}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);

        // configuration (edited via EditorBotAgentComponent)
        AZ::EntityId m_targetEntity;
        AZStd::string m_modelPath;        //!< project-relative path to a .weights.json MLP; empty = heuristic
        float m_moveSpeed = 6.0f;         //!< m/s
        float m_preferredRange = 12.0f;   //!< heuristic keeps roughly this distance
        float m_eyeHeight = 1.6f;         //!< shot origin above entity origin
        float m_fireRange = 200.0f;
        float m_fireDamage = 10.0f;
        HumanConstraints m_constraints;

    protected:
        void Activate() override;
        void Deactivate() override;

        // AZ::TickBus
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;

    private:
        struct Observation
        {
            float m_age = 0.0f; //!< grows every tick until it exceeds reaction time
            AZStd::vector<float> m_values;
            AZ::Vector3 m_targetPosition = AZ::Vector3::CreateZero();
            bool m_lineOfSight = false;
        };

        Observation Perceive();
        void DecideHeuristic(const Observation& obs, AZStd::vector<float>& action) const;
        void Act(const Observation& obs, const AZStd::vector<float>& action, float deltaTime);
        void Shoot(const AZ::Vector3& eye, const AZ::Vector3& direction);
        bool HasLineOfSight(const AZ::Vector3& from, const AZ::Vector3& to) const;

        MlpPolicy m_policy;
        AZStd::deque<Observation> m_perceptionQueue; //!< reaction-time delay line
        AZ::SimpleLcgRandom m_random;
        float m_fireCooldown = 0.0f;
        float m_timeSinceSeen = 0.0f;
        float m_yawDegrees = 0.0f;
        bool m_yawInitialized = false;
    };
} // namespace NeuralBots
