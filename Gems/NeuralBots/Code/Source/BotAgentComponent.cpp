/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include "BotAgentComponent.h"

#include <AzCore/Component/TransformBus.h>
#include <AzCore/Math/Quaternion.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Time/ITime.h>
#include <AzFramework/Physics/CharacterBus.h>
#include <AzFramework/Physics/Common/PhysicsSceneQueries.h>
#include <AzFramework/Physics/PhysicsScene.h>
#include <AzFramework/Physics/PhysicsSystem.h>
#include <LmbrCentral/Scripting/GameplayNotificationBus.h>

namespace NeuralBots
{
    void HumanConstraints::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<HumanConstraints>()
                ->Version(1)
                ->Field("ReactionTime", &HumanConstraints::m_reactionTime)
                ->Field("AimErrorDegrees", &HumanConstraints::m_aimErrorDegrees)
                ->Field("MaxTurnDegPerSec", &HumanConstraints::m_maxTurnDegPerSec)
                ->Field("MaxShotsPerSecond", &HumanConstraints::m_maxShotsPerSecond);

            if (auto* editContext = serializeContext->GetEditContext())
            {
                editContext->Class<HumanConstraints>("Human constraints", "Limits that make the bot behave like a human player")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &HumanConstraints::m_reactionTime,
                        "Reaction time (s)", "Delay between the world changing and the bot perceiving it")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &HumanConstraints::m_aimErrorDegrees,
                        "Aim error (deg)", "Standard deviation of Gaussian aim noise applied to every shot")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &HumanConstraints::m_maxTurnDegPerSec,
                        "Max turn speed (deg/s)", "How fast the bot can rotate its view")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &HumanConstraints::m_maxShotsPerSecond,
                        "Max shots per second", "Fire-rate cap");
            }
        }
    }

    void BotAgentComponent::Reflect(AZ::ReflectContext* context)
    {
        HumanConstraints::Reflect(context);

        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<BotAgentComponent, AZ::Component>()
                ->Version(1)
                ->Field("TargetEntity", &BotAgentComponent::m_targetEntity)
                ->Field("ModelPath", &BotAgentComponent::m_modelPath)
                ->Field("MoveSpeed", &BotAgentComponent::m_moveSpeed)
                ->Field("PreferredRange", &BotAgentComponent::m_preferredRange)
                ->Field("EyeHeight", &BotAgentComponent::m_eyeHeight)
                ->Field("FireRange", &BotAgentComponent::m_fireRange)
                ->Field("FireDamage", &BotAgentComponent::m_fireDamage)
                ->Field("Constraints", &BotAgentComponent::m_constraints);

            if (auto* editContext = serializeContext->GetEditContext())
            {
                editContext->Class<BotAgentComponent>("Bot Agent", "Neural-net-driven combatant with human constraints")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Category, "AI")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &BotAgentComponent::m_targetEntity,
                        "Target entity", "The entity this bot fights (e.g. the player)")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &BotAgentComponent::m_modelPath,
                        "Model file", "Project-relative path to an AIBackbone .weights.json MLP; leave empty for the built-in heuristic")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &BotAgentComponent::m_moveSpeed,
                        "Move speed (m/s)", "")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &BotAgentComponent::m_preferredRange,
                        "Preferred range (m)", "Heuristic keeps roughly this distance to the target")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &BotAgentComponent::m_eyeHeight,
                        "Eye height (m)", "Shot/vision origin above the entity origin")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &BotAgentComponent::m_fireRange,
                        "Fire range (m)", "")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &BotAgentComponent::m_fireDamage,
                        "Fire damage", "Sent as a 'Damage' gameplay event to whatever the shot hits")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &BotAgentComponent::m_constraints,
                        "Human constraints", "");
            }
        }
    }

    void BotAgentComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("BotAgentService"));
    }

    void BotAgentComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("BotAgentService"));
    }

    void BotAgentComponent::Activate()
    {
        if (!m_modelPath.empty())
        {
            if (!m_policy.LoadFromFile(m_modelPath))
            {
                AZ_Warning("NeuralBots", false,
                    "Bot Agent on entity %s: failed to load model '%s'; falling back to the built-in heuristic.",
                    GetEntityId().ToString().c_str(), m_modelPath.c_str());
            }
            else if (m_policy.InputWidth() != Policy::InputWidth || m_policy.OutputWidth() != Policy::OutputWidth)
            {
                AZ_Warning("NeuralBots", false,
                    "Bot Agent on entity %s: model '%s' has %zu inputs / %zu outputs (expected %zu / %zu); "
                    "falling back to the built-in heuristic.",
                    GetEntityId().ToString().c_str(), m_modelPath.c_str(),
                    m_policy.InputWidth(), m_policy.OutputWidth(), Policy::InputWidth, Policy::OutputWidth);
                m_policy.Clear();
            }
        }

        m_perceptionQueue.clear();
        m_fireCooldown = 0.0f;
        m_timeSinceSeen = 10.0f;
        m_yawInitialized = false;
        if (auto* timeSystem = AZ::Interface<AZ::ITime>::Get())
        {
            m_random.SetSeed(static_cast<AZ::u64>(timeSystem->GetElapsedTimeUs()) ^
                static_cast<AZ::u64>(GetEntityId()));
        }

        AZ::TickBus::Handler::BusConnect();
    }

    void BotAgentComponent::Deactivate()
    {
        AZ::TickBus::Handler::BusDisconnect();
        m_policy.Clear();
        m_perceptionQueue.clear();
    }

    bool BotAgentComponent::HasLineOfSight(const AZ::Vector3& from, const AZ::Vector3& to) const
    {
        auto* physicsSystem = AZ::Interface<AzPhysics::SystemInterface>::Get();
        if (!physicsSystem)
        {
            return false;
        }
        AzPhysics::SceneHandle sceneHandle = physicsSystem->GetSceneHandle(AzPhysics::DefaultPhysicsSceneName);
        AzPhysics::Scene* scene = physicsSystem->GetScene(sceneHandle);
        if (!scene)
        {
            return false;
        }

        const AZ::Vector3 delta = to - from;
        const float distance = delta.GetLength();
        if (distance < 0.01f)
        {
            return true;
        }

        AzPhysics::RayCastRequest request;
        request.m_start = from;
        request.m_direction = delta / distance;
        request.m_distance = distance;
        request.m_reportMultipleHits = true;

        AzPhysics::SceneQueryHits hits = scene->QueryScene(&request);
        for (const AzPhysics::SceneQueryHit& hit : hits.m_hits)
        {
            if (hit.m_entityId != GetEntityId() && hit.m_entityId != m_targetEntity)
            {
                return false; // something solid between us and the target
            }
        }
        return true;
    }

    BotAgentComponent::Observation BotAgentComponent::Perceive()
    {
        Observation obs;

        AZ::Transform selfTm = AZ::Transform::CreateIdentity();
        AZ::TransformBus::EventResult(selfTm, GetEntityId(), &AZ::TransformBus::Events::GetWorldTM);
        AZ::Vector3 targetPos = AZ::Vector3::CreateZero();
        AZ::TransformBus::EventResult(targetPos, m_targetEntity, &AZ::TransformBus::Events::GetWorldTranslation);

        const AZ::Vector3 selfPos = selfTm.GetTranslation();
        const AZ::Vector3 eye = selfPos + AZ::Vector3(0.0f, 0.0f, m_eyeHeight);
        const AZ::Vector3 targetEye = targetPos + AZ::Vector3(0.0f, 0.0f, m_eyeHeight);

        AZ::Vector3 toTarget = targetPos - selfPos;
        const float distance = toTarget.GetLength();
        AZ::Vector3 toTargetDir = distance > 0.001f ? toTarget / distance : AZ::Vector3::CreateAxisY();

        // into bot-local space
        const AZ::Quaternion invRot = selfTm.GetRotation().GetInverseFull();
        const AZ::Vector3 localDir = invRot.TransformVector(toTargetDir);

        const bool los = m_targetEntity.IsValid() && HasLineOfSight(eye, targetEye);
        m_timeSinceSeen = los ? 0.0f : m_timeSinceSeen;

        AZ::Vector3 velocity = AZ::Vector3::CreateZero();
        Physics::CharacterRequestBus::EventResult(velocity, GetEntityId(), &Physics::CharacterRequests::GetVelocity);

        const float cosAngle = selfTm.GetBasisY().Dot(toTargetDir);

        obs.m_values = {
            localDir.GetX(),
            localDir.GetY(),
            localDir.GetZ(),
            AZStd::min(distance / 50.0f, 2.0f),
            los ? 1.0f : 0.0f,
            AZStd::min(velocity.GetLength() / 10.0f, 2.0f),
            cosAngle,
            AZStd::min(m_timeSinceSeen / 2.0f, 1.0f),
        };
        obs.m_targetPosition = targetPos;
        obs.m_lineOfSight = los;
        return obs;
    }

    void BotAgentComponent::DecideHeuristic(const Observation& obs, AZStd::vector<float>& action) const
    {
        action.assign(Policy::OutputWidth, 0.0f);

        const float localX = obs.m_values[0];
        const float localY = obs.m_values[1];
        const float distance = obs.m_values[3] * 50.0f;
        const bool los = obs.m_values[4] > 0.5f;
        const float cosAngle = obs.m_values[6];

        // turn toward the target (sign of the local X of the target direction)
        action[2] = AZ::GetClamp(-localX * 4.0f, -1.0f, 1.0f);

        if (los)
        {
            // hold preferred range: advance when far, back off when close, always strafe
            const float rangeError = distance - m_preferredRange;
            action[1] = AZ::GetClamp(rangeError * 0.2f, -1.0f, 1.0f);
            action[0] = (((static_cast<AZ::u32>(static_cast<AZ::u64>(GetEntityId())) >> 4) & 1) != 0) ? 0.7f : -0.7f;
            // shoot when roughly facing the target
            action[3] = (cosAngle > 0.94f && distance <= m_fireRange) ? 1.0f : 0.0f;
        }
        else
        {
            // no line of sight: push toward the target's last known position
            action[0] = AZ::GetClamp(localX * 2.0f, -1.0f, 1.0f);
            action[1] = AZ::GetClamp(localY * 2.0f, -1.0f, 1.0f);
        }
    }

    void BotAgentComponent::Shoot(const AZ::Vector3& eye, const AZ::Vector3& direction)
    {
        auto* physicsSystem = AZ::Interface<AzPhysics::SystemInterface>::Get();
        if (!physicsSystem)
        {
            return;
        }
        AzPhysics::Scene* scene = physicsSystem->GetScene(physicsSystem->GetSceneHandle(AzPhysics::DefaultPhysicsSceneName));
        if (!scene)
        {
            return;
        }

        AzPhysics::RayCastRequest request;
        request.m_start = eye;
        request.m_direction = direction;
        request.m_distance = m_fireRange;
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
            AZ::GameplayNotificationBus::Event(
                AZ::GameplayNotificationId(closest->m_entityId, AZ_CRC_CE("Damage"), azrtti_typeid<float>()),
                &AZ::GameplayNotificationBus::Events::OnEventBegin,
                AZStd::any(m_fireDamage));
        }
    }

    void BotAgentComponent::Act(const Observation& obs, const AZStd::vector<float>& action, float deltaTime)
    {
        AZ::Transform selfTm = AZ::Transform::CreateIdentity();
        AZ::TransformBus::EventResult(selfTm, GetEntityId(), &AZ::TransformBus::Events::GetWorldTM);

        if (!m_yawInitialized)
        {
            m_yawDegrees = AZ::RadToDeg(selfTm.GetRotation().GetEulerRadians().GetZ());
            m_yawInitialized = true;
        }

        // turn, clamped to human turn speed
        const float turnRequest = AZ::GetClamp(action[2], -1.0f, 1.0f) * m_constraints.m_maxTurnDegPerSec;
        m_yawDegrees += turnRequest * deltaTime;
        AZ::TransformBus::Event(GetEntityId(), &AZ::TransformBus::Events::SetLocalRotation,
            AZ::Vector3(0.0f, 0.0f, AZ::DegToRad(m_yawDegrees)));

        // move through the character controller, like a player would
        const AZ::Quaternion yawRot = AZ::Quaternion::CreateRotationZ(AZ::DegToRad(m_yawDegrees));
        AZ::Vector3 moveLocal(AZ::GetClamp(action[0], -1.0f, 1.0f), AZ::GetClamp(action[1], -1.0f, 1.0f), 0.0f);
        if (moveLocal.GetLengthSq() > 1.0f)
        {
            moveLocal.Normalize();
        }
        const AZ::Vector3 moveWorld = yawRot.TransformVector(moveLocal) * m_moveSpeed;
        Physics::CharacterRequestBus::Event(GetEntityId(), &Physics::CharacterRequests::AddVelocityForTick, moveWorld);

        // shoot, with fire-rate cap and Gaussian aim error
        m_fireCooldown -= deltaTime;
        const bool wantShoot = action[3] > 0.5f;
        if (wantShoot && obs.m_lineOfSight && m_fireCooldown <= 0.0f && m_constraints.m_maxShotsPerSecond > 0.0f)
        {
            m_fireCooldown = 1.0f / m_constraints.m_maxShotsPerSecond;

            const AZ::Vector3 eye = selfTm.GetTranslation() + AZ::Vector3(0.0f, 0.0f, m_eyeHeight);
            AZ::Vector3 aim = (obs.m_targetPosition + AZ::Vector3(0.0f, 0.0f, m_eyeHeight)) - eye;
            if (!aim.IsZero())
            {
                aim.Normalize();
                // Box-Muller Gaussian noise on yaw and pitch
                const float u1 = AZStd::max(m_random.GetRandomFloat(), 1e-6f);
                const float u2 = m_random.GetRandomFloat();
                const float mag = sqrtf(-2.0f * logf(u1));
                const float errYaw = AZ::DegToRad(m_constraints.m_aimErrorDegrees) * mag * cosf(AZ::Constants::TwoPi * u2);
                const float errPitch = AZ::DegToRad(m_constraints.m_aimErrorDegrees) * mag * sinf(AZ::Constants::TwoPi * u2);
                const AZ::Quaternion noise =
                    AZ::Quaternion::CreateRotationZ(errYaw) * AZ::Quaternion::CreateRotationX(errPitch);
                aim = noise.TransformVector(aim);
                Shoot(eye, aim);
            }
        }
    }

    void BotAgentComponent::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        if (!m_targetEntity.IsValid())
        {
            return;
        }

        m_timeSinceSeen += deltaTime;

        // perceive now, act on what we perceived reaction-time ago
        m_perceptionQueue.push_back(Perceive());
        for (Observation& queued : m_perceptionQueue)
        {
            queued.m_age += deltaTime;
        }
        while (m_perceptionQueue.size() > 1 && m_perceptionQueue.front().m_age > m_constraints.m_reactionTime &&
               m_perceptionQueue[1].m_age >= m_constraints.m_reactionTime)
        {
            m_perceptionQueue.pop_front();
        }
        const Observation& obs = m_perceptionQueue.front();
        if (obs.m_age < m_constraints.m_reactionTime)
        {
            return; // still "reacting"
        }

        AZStd::vector<float> action;
        if (m_policy.IsLoaded())
        {
            if (!m_policy.Evaluate(obs.m_values, action))
            {
                DecideHeuristic(obs, action);
            }
        }
        else
        {
            DecideHeuristic(obs, action);
        }

        Act(obs, action, deltaTime);
    }
} // namespace NeuralBots
