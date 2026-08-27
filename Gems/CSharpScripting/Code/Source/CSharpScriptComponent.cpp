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
        ScriptHost::Instance().ScriptOnUpdate(m_handle, deltaTime);
    }
} // namespace CSharpScripting
