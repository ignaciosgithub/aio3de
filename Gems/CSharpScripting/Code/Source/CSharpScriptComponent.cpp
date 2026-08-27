/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "CSharpScriptComponent.h"
#include "ScriptHost.h"

#include <AzCore/Component/Entity.h>
#include <AzCore/Console/IConsole.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Interface/Interface.h>
#include <AzFramework/Physics/Common/PhysicsSimulatedBody.h>
#include <AzFramework/Physics/Components/SimulatedBodyComponentBus.h>
#include <AzFramework/Physics/PhysicsScene.h>

namespace CSharpScripting
{
    static void csharp_rebuild([[maybe_unused]] const AZ::ConsoleCommandContainer& arguments)
    {
        ScriptHost::Instance().RebuildScripts();
    }
    AZ_CONSOLEFREEFUNC(csharp_rebuild, AZ::ConsoleFunctorFlags::Null, "Recompile and reload the project's C# scripts");

    void CSharpScriptComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<CSharpScriptComponent, AZ::Component>()
                ->Version(1)
                ->Field("ClassName", &CSharpScriptComponent::m_className);
        }
    }

    void CSharpScriptComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("CSharpScriptService"));
    }

    void CSharpScriptComponent::GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required)
    {
        required.push_back(AZ_CRC_CE("TransformService"));
    }

    CSharpScriptComponent::CSharpScriptComponent(const AZStd::string& className)
        : m_className(className)
    {
    }

    void CSharpScriptComponent::Activate()
    {
        if (m_className.empty())
        {
            AZ_Warning("CSharpScripting", false, "C# Script component on entity '%s' has no class name set.", GetEntity()->GetName().c_str());
            return;
        }
        m_handle = ScriptHost::Instance().CreateScript(m_className, GetEntityId());
        if (m_handle != 0)
        {
            ScriptHost::Instance().ScriptOnActivate(m_handle);
            AZ::TickBus::Handler::BusConnect();
        }
    }

    void CSharpScriptComponent::Deactivate()
    {
        m_collisionBeginHandler.Disconnect();
        m_collisionEndHandler.Disconnect();
        m_collisionHandlersRegistered = false;
        if (m_handle != 0)
        {
            AZ::TickBus::Handler::BusDisconnect();
            ScriptHost::Instance().ScriptOnDeactivate(m_handle);
            ScriptHost::Instance().DestroyScript(m_handle);
            m_handle = 0;
        }
    }

    void CSharpScriptComponent::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        if (!m_collisionHandlersRegistered)
        {
            RegisterCollisionHandlers();
        }
        ScriptHost::Instance().ScriptOnUpdate(m_handle, deltaTime);
    }

    void CSharpScriptComponent::RegisterCollisionHandlers()
    {
        AzPhysics::SimulatedBodyHandle bodyHandle = AzPhysics::InvalidSimulatedBodyHandle;
        AzPhysics::SimulatedBodyComponentRequestsBus::EventResult(
            bodyHandle, GetEntityId(), &AzPhysics::SimulatedBodyComponentRequests::GetSimulatedBodyHandle);
        if (bodyHandle == AzPhysics::InvalidSimulatedBodyHandle)
        {
            return;
        }

        AzPhysics::SceneHandle sceneHandle = AzPhysics::InvalidSceneHandle;
        if (auto* sceneInterface = AZ::Interface<AzPhysics::SceneInterface>::Get())
        {
            sceneHandle = sceneInterface->GetSceneHandle(AzPhysics::DefaultPhysicsSceneName);
        }
        if (sceneHandle == AzPhysics::InvalidSceneHandle)
        {
            return;
        }

        const AZ::s64 handle = m_handle;
        m_collisionBeginHandler = AzPhysics::SimulatedBodyEvents::OnCollisionBegin::Handler(
            [handle]([[maybe_unused]] AzPhysics::SimulatedBodyHandle bodyHandle, const AzPhysics::CollisionEvent& event)
            {
                const AZ::u64 otherEntityId = event.m_body2 ? static_cast<AZ::u64>(event.m_body2->GetEntityId()) : 0;
                AZ::Vector3 position = AZ::Vector3::CreateZero();
                AZ::Vector3 normal = AZ::Vector3::CreateZero();
                float impulse = 0.0f;
                if (!event.m_contacts.empty())
                {
                    position = event.m_contacts.front().m_position;
                    normal = event.m_contacts.front().m_normal;
                    impulse = event.m_contacts.front().m_impulse.GetLength();
                }
                ScriptHost::Instance().ScriptOnCollisionEnter(
                    handle, otherEntityId,
                    position.GetX(), position.GetY(), position.GetZ(),
                    normal.GetX(), normal.GetY(), normal.GetZ(), impulse);
            });
        m_collisionEndHandler = AzPhysics::SimulatedBodyEvents::OnCollisionEnd::Handler(
            [handle]([[maybe_unused]] AzPhysics::SimulatedBodyHandle bodyHandle, const AzPhysics::CollisionEvent& event)
            {
                const AZ::u64 otherEntityId = event.m_body2 ? static_cast<AZ::u64>(event.m_body2->GetEntityId()) : 0;
                ScriptHost::Instance().ScriptOnCollisionExit(handle, otherEntityId);
            });
        AzPhysics::SimulatedBodyEvents::RegisterOnCollisionBeginHandler(sceneHandle, bodyHandle, m_collisionBeginHandler);
        AzPhysics::SimulatedBodyEvents::RegisterOnCollisionEndHandler(sceneHandle, bodyHandle, m_collisionEndHandler);
        m_collisionHandlersRegistered = true;
    }
} // namespace CSharpScripting
