/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/std/string/string.h>
#include <AzToolsFramework/ToolsComponents/EditorComponentBase.h>

namespace CSharpScripting
{
    //! Editor counterpart of CSharpScriptComponent: pick the C# class to run and rebuild scripts.
    class EditorCSharpScriptComponent
        : public AzToolsFramework::Components::EditorComponentBase
    {
    public:
        AZ_EDITOR_COMPONENT(EditorCSharpScriptComponent, "{8B2C4D6E-3F50-47B9-AC1D-E2F3A4B5C6D7}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required);

        // EditorComponentBase
        void BuildGameEntity(AZ::Entity* gameEntity) override;

    private:
        AZ::Crc32 OnRebuildPressed();

        AZStd::string m_className;
    };
} // namespace CSharpScripting
