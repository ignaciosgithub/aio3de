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
#include <AzCore/std/string/string.h>
#include <AzFramework/Physics/Common/PhysicsSimulatedBodyEvents.h>

namespace CSharpScripting
{
    //! Runs a C# behaviour class (from <project>/Scripts) on this entity in game mode.
    //! The class derives from AIO3DE.ScriptComponent and gets OnActivate/OnUpdate/OnDeactivate.
    class CSharpScriptComponent
        : public AZ::Component
        , private AZ::TickBus::Handler
    {
    public:
        AZ_COMPONENT(CSharpScriptComponent, "{7A1B3C5D-2E4F-46A8-9B0C-D1E2F3A4B5C6}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required);

        CSharpScriptComponent() = default;
        explicit CSharpScriptComponent(const AZStd::string& className);

    protected:
        void Activate() override;
        void Deactivate() override;

        // TickBus
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;

    private:
        void RegisterCollisionHandlers();

        AZStd::string m_className;
        AZ::s64 m_handle = 0;
        bool m_collisionHandlersRegistered = false;
        AzPhysics::SimulatedBodyEvents::OnCollisionBegin::Handler m_collisionBeginHandler;
        AzPhysics::SimulatedBodyEvents::OnCollisionEnd::Handler m_collisionEndHandler;
        AzPhysics::SimulatedBodyEvents::OnTriggerEnter::Handler m_triggerEnterHandler;
        AzPhysics::SimulatedBodyEvents::OnTriggerExit::Handler m_triggerExitHandler;
    };
} // namespace CSharpScripting
