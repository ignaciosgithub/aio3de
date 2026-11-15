/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "BehaviorTreeComponent.h"

#include "BehaviorTreeParser.h"

#include <AzCore/Component/TransformBus.h>
#include <AzCore/Console/ILogger.h>
#include <AzCore/Math/MathUtils.h>
#include <AzCore/RTTI/BehaviorContext.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Utils/Utils.h>
#include <AzFramework/Physics/CharacterBus.h>
#include <VoxelNav/VoxelNavBus.h>

namespace BehaviorTree
{
    struct BehaviorTreeStatusConstants
    {
        AZ_TYPE_INFO(BehaviorTreeStatusConstants, "{A793223F-36CF-4922-9E99-ED3201B4D539}");
    };

    class BehaviorTreeActionNotificationBusHandler
        : public BehaviorTreeActionNotificationBus::Handler
        , public AZ::BehaviorEBusHandler
    {
    public:
        AZ_EBUS_BEHAVIOR_BINDER(
            BehaviorTreeActionNotificationBusHandler, "{BFE55F57-9C1E-4D61-A797-E1F0F79C2015}", AZ::SystemAllocator,
            OnActionTick, OnActionAbort, OnCondition);

        int OnActionTick(const AZStd::string& action, float deltaTime) override
        {
            int result = static_cast<int>(NodeStatus::Failure);
            CallResult(result, FN_OnActionTick, action, deltaTime);
            return result;
        }

        void OnActionAbort(const AZStd::string& action) override
        {
            Call(FN_OnActionAbort, action);
        }

        bool OnCondition(const AZStd::string& condition) override
        {
            bool result = false;
            CallResult(result, FN_OnCondition, condition);
            return result;
        }
    };

    void BehaviorTreeComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<BehaviorTreeComponent, AZ::Component>()
                ->Version(1)
                ->Field("TreeFile", &BehaviorTreeComponent::m_treeFile)
                ->Field("TickInterval", &BehaviorTreeComponent::m_tickInterval)
                ->Field("StartActive", &BehaviorTreeComponent::m_startActive)
                ->Field("DebugLogTransitions", &BehaviorTreeComponent::m_debugLogTransitions);

            if (AZ::EditContext* editContext = serializeContext->GetEditContext())
            {
                editContext->Class<BehaviorTreeComponent>("Behavior Tree", "Runs a data-driven behavior tree for this entity")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                        ->Attribute(AZ::Edit::Attributes::Category, "AI")
                        ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                        ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &BehaviorTreeComponent::m_treeFile, "Tree file",
                        "Project-relative .behaviortree.json path")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &BehaviorTreeComponent::m_tickInterval, "Tick interval (s)",
                        "Zero ticks every frame")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &BehaviorTreeComponent::m_startActive, "Start active", "Load and run on activation")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &BehaviorTreeComponent::m_debugLogTransitions, "Debug log transitions",
                        "Log status changes");
            }
        }

        if (auto* behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behaviorContext->Class<BehaviorTreeStatusConstants>("BehaviorTreeStatus")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Category, "AI")
                ->Enum<static_cast<int>(NodeStatus::Success)>("Success")
                ->Enum<static_cast<int>(NodeStatus::Failure)>("Failure")
                ->Enum<static_cast<int>(NodeStatus::Running)>("Running");

            behaviorContext->EBus<BehaviorTreeRequestBus>("BehaviorTreeRequestBus")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Category, "AI")
                ->Event("SetBlackboardBool", &BehaviorTreeRequestBus::Events::SetBlackboardBool)
                ->Event("SetBlackboardFloat", &BehaviorTreeRequestBus::Events::SetBlackboardFloat)
                ->Event("SetBlackboardString", &BehaviorTreeRequestBus::Events::SetBlackboardString)
                ->Event("SetBlackboardVector3", &BehaviorTreeRequestBus::Events::SetBlackboardVector3)
                ->Event("SetBlackboardEntityId", &BehaviorTreeRequestBus::Events::SetBlackboardEntityId)
                ->Event("GetBlackboardBool", &BehaviorTreeRequestBus::Events::GetBlackboardBool)
                ->Event("GetBlackboardFloat", &BehaviorTreeRequestBus::Events::GetBlackboardFloat)
                ->Event("GetBlackboardString", &BehaviorTreeRequestBus::Events::GetBlackboardString)
                ->Event("GetBlackboardVector3", &BehaviorTreeRequestBus::Events::GetBlackboardVector3)
                ->Event("GetBlackboardEntityId", &BehaviorTreeRequestBus::Events::GetBlackboardEntityId)
                ->Event("HasBlackboardKey", &BehaviorTreeRequestBus::Events::HasBlackboardKey)
                ->Event("Restart", &BehaviorTreeRequestBus::Events::Restart)
                ->Event("SetPaused", &BehaviorTreeRequestBus::Events::SetPaused)
                ->Event("IsRunning", &BehaviorTreeRequestBus::Events::IsRunning)
                ->Event("GetActiveNodePaths", &BehaviorTreeRequestBus::Events::GetActiveNodePaths);

            behaviorContext->EBus<BehaviorTreeActionNotificationBus>("BehaviorTreeActionNotificationBus")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Category, "AI")
                ->Handler<BehaviorTreeActionNotificationBusHandler>();
        }
    }

    void BehaviorTreeComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("BehaviorTreeService"));
    }

    void BehaviorTreeComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("BehaviorTreeService"));
    }

    void BehaviorTreeComponent::SetConfiguration(
        const AZStd::string& treeFile, float tickInterval, bool startActive, bool debugLogTransitions)
    {
        m_treeFile = treeFile;
        m_tickInterval = tickInterval;
        m_startActive = startActive;
        m_debugLogTransitions = debugLogTransitions;
    }

    void BehaviorTreeComponent::Activate()
    {
        BehaviorTreeRequestBus::Handler::BusConnect(GetEntityId());
        if (m_startActive)
        {
            LoadTree();
        }
    }

    void BehaviorTreeComponent::Deactivate()
    {
        m_desiredVelocity = AZ::Vector3::CreateZero();
        AZ::TickBus::Handler::BusDisconnect();
        BehaviorTreeRequestBus::Handler::BusDisconnect();
        if (m_tree)
        {
            m_tree->Restart(*this);
            m_tree.reset();
        }
    }

    bool BehaviorTreeComponent::LoadTree()
    {
        AZStd::string fullPath = m_treeFile;
        if (!fullPath.empty() && fullPath[0] != '/' && fullPath.find(':') == AZStd::string::npos)
        {
            fullPath = AZStd::string::format("%s/%s", AZ::Utils::GetProjectPath().c_str(), m_treeFile.c_str());
        }

        BehaviorTreeParser::ParseOutcome result = BehaviorTreeParser::LoadFromFile(fullPath);
        if (!result.IsSuccess())
        {
            if (!m_loadErrorLogged)
            {
                AZ_Error("BehaviorTree", false, "Entity %s could not load tree '%s': %s",
                    GetEntityId().ToString().c_str(), fullPath.c_str(), result.GetError().c_str());
                m_loadErrorLogged = true;
            }
            return false;
        }

        m_tree = AZStd::make_unique<TreeInstance>(result.TakeValue());
        m_tickAccumulator = 0.0f;
        m_hasPreviousStatus = false;
        m_loadErrorLogged = false;
        AZ::TickBus::Handler::BusConnect();
        return true;
    }

    void BehaviorTreeComponent::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        if (!m_tree || m_paused)
        {
            return;
        }

        Physics::CharacterRequests* character = Physics::CharacterRequestBus::FindFirstHandler(GetEntityId());
        if (character && character->IsPresent())
        {
            Physics::CharacterRequestBus::Event(
                GetEntityId(), &Physics::CharacterRequests::AddVelocityForTick, m_desiredVelocity);
        }
        else if (!m_desiredVelocity.IsZero())
        {
            const AZ::Vector3 position = GetPosition();
            AZ::TransformBus::Event(
                GetEntityId(), &AZ::TransformBus::Events::SetWorldTranslation,
                position + m_desiredVelocity * deltaTime);
        }

        m_tickAccumulator += deltaTime;
        if (m_tickInterval > 0.0f && m_tickAccumulator < m_tickInterval)
        {
            return;
        }
        const float tickDeltaTime = m_tickAccumulator;
        m_tickAccumulator = 0.0f;
        m_desiredVelocity = AZ::Vector3::CreateZero();

        const NodeStatus status = m_tree->Tick(tickDeltaTime, *this);
        if (m_debugLogTransitions && (!m_hasPreviousStatus || status != m_previousStatus))
        {
            AZ_TracePrintf("BehaviorTree", "Entity %s tree status changed to %d\n",
                GetEntityId().ToString().c_str(), static_cast<int>(status));
        }
        m_previousStatus = status;
        m_hasPreviousStatus = true;
    }

    void BehaviorTreeComponent::SetBlackboardBool(const AZStd::string& name, bool value)
    {
        if (m_tree) m_tree->GetBlackboard().SetBool(name, value);
    }

    void BehaviorTreeComponent::SetBlackboardFloat(const AZStd::string& name, float value)
    {
        if (m_tree) m_tree->GetBlackboard().SetFloat(name, value);
    }

    void BehaviorTreeComponent::SetBlackboardString(const AZStd::string& name, const AZStd::string& value)
    {
        if (m_tree) m_tree->GetBlackboard().SetString(name, value);
    }

    void BehaviorTreeComponent::SetBlackboardVector3(const AZStd::string& name, const AZ::Vector3& value)
    {
        if (m_tree) m_tree->GetBlackboard().SetVector3(name, value);
    }

    void BehaviorTreeComponent::SetBlackboardEntityId(const AZStd::string& name, const AZ::EntityId& value)
    {
        if (m_tree) m_tree->GetBlackboard().SetEntityId(name, value);
    }

    bool BehaviorTreeComponent::GetBlackboardBool(const AZStd::string& name)
    {
        return m_tree ? m_tree->GetBlackboard().GetBool(name) : false;
    }

    float BehaviorTreeComponent::GetBlackboardFloat(const AZStd::string& name)
    {
        return m_tree ? m_tree->GetBlackboard().GetFloat(name) : 0.0f;
    }

    AZStd::string BehaviorTreeComponent::GetBlackboardString(const AZStd::string& name)
    {
        return m_tree ? m_tree->GetBlackboard().GetString(name) : AZStd::string{};
    }

    AZ::Vector3 BehaviorTreeComponent::GetBlackboardVector3(const AZStd::string& name)
    {
        return m_tree ? m_tree->GetBlackboard().GetVector3(name) : AZ::Vector3::CreateZero();
    }

    AZ::EntityId BehaviorTreeComponent::GetBlackboardEntityId(const AZStd::string& name)
    {
        return m_tree ? m_tree->GetBlackboard().GetEntityId(name) : AZ::EntityId{};
    }

    bool BehaviorTreeComponent::HasBlackboardKey(const AZStd::string& name)
    {
        return m_tree && m_tree->GetBlackboard().Has(name);
    }

    void BehaviorTreeComponent::Restart()
    {
        if (m_tree)
        {
            m_tree->Restart(*this);
        }
        else
        {
            LoadTree();
        }
    }

    void BehaviorTreeComponent::SetPaused(bool paused)
    {
        m_paused = paused;
        if (paused)
        {
            m_desiredVelocity = AZ::Vector3::CreateZero();
        }
    }

    bool BehaviorTreeComponent::IsRunning()
    {
        return m_tree && m_tree->IsRunning();
    }

    AZStd::vector<AZStd::string> BehaviorTreeComponent::GetActiveNodePaths()
    {
        return m_tree ? m_tree->GetActiveNodePaths() : AZStd::vector<AZStd::string>{};
    }

    AZ::Vector3 BehaviorTreeComponent::GetPosition() const
    {
        AZ::Vector3 position = AZ::Vector3::CreateZero();
        AZ::TransformBus::EventResult(position, GetEntityId(), &AZ::TransformBus::Events::GetWorldTranslation);
        return position;
    }

    AZStd::vector<AZ::Vector3> BehaviorTreeComponent::FindPath(const AZ::Vector3& from, const AZ::Vector3& to)
    {
        AZStd::vector<AZ::Vector3> path;
        VoxelNav::VoxelNavRequestBus::BroadcastResult(
            path, &VoxelNav::VoxelNavRequests::FindPath, from, to);
        return path;
    }

    void BehaviorTreeComponent::MoveToward(const AZ::Vector3& direction, float speed)
    {
        m_desiredVelocity = direction * speed;
    }

    void BehaviorTreeComponent::StopMoving()
    {
        m_desiredVelocity = AZ::Vector3::CreateZero();
    }

    bool BehaviorTreeComponent::GetEntityPosition(const AZ::EntityId& entityId, AZ::Vector3& outPosition) const
    {
        if (!AZ::TransformBus::FindFirstHandler(entityId))
        {
            return false;
        }
        AZ::TransformBus::EventResult(outPosition, entityId, &AZ::TransformBus::Events::GetWorldTranslation);
        return true;
    }

    int BehaviorTreeComponent::TickAction(const AZStd::string& action, float deltaTime)
    {
        if (!BehaviorTreeActionNotificationBus::FindFirstHandler(GetEntityId()))
        {
            if (m_warnedMissingActions.insert(action).second)
            {
                AZ_Warning("BehaviorTree", false, "No BehaviorTreeActionNotificationBus handler for action '%s'", action.c_str());
            }
            return static_cast<int>(NodeStatus::Failure);
        }
        int result = static_cast<int>(NodeStatus::Failure);
        BehaviorTreeActionNotificationBus::EventResult(
            result, GetEntityId(), &BehaviorTreeActionNotifications::OnActionTick, action, deltaTime);
        return result;
    }

    void BehaviorTreeComponent::AbortAction(const AZStd::string& action)
    {
        BehaviorTreeActionNotificationBus::Event(
            GetEntityId(), &BehaviorTreeActionNotifications::OnActionAbort, action);
    }

    bool BehaviorTreeComponent::EvaluateCondition(const AZStd::string& condition)
    {
        if (!BehaviorTreeActionNotificationBus::FindFirstHandler(GetEntityId()))
        {
            return false;
        }
        bool result = false;
        BehaviorTreeActionNotificationBus::EventResult(
            result, GetEntityId(), &BehaviorTreeActionNotifications::OnCondition, condition);
        return result;
    }

    void BehaviorTreeComponent::Log(const AZStd::string& message)
    {
        AZ_TracePrintf("BehaviorTree", "%s\n", message.c_str());
    }
} // namespace BehaviorTree
