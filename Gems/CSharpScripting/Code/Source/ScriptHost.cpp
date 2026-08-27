/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "ScriptHost.h"

#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Debug/Trace.h>
#include <AzCore/IO/SystemFile.h>
#include <AzCore/Math/Quaternion.h>
#include <AzCore/Math/Transform.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/StringFunc/StringFunc.h>
#include <AzCore/Utils/Utils.h>

#include <cstdio>

namespace CSharpScripting
{
    namespace
    {
        void ApiLog(int level, const char* message)
        {
            switch (level)
            {
            case 2:
                AZ_Error("C#", false, "%s", message);
                break;
            case 1:
                AZ_Warning("C#", false, "%s", message);
                break;
            default:
                AZ_Printf("C#", "%s", message);
                break;
            }
        }

        void ApiGetWorldPosition(AZ::u64 entityId, float* xyz)
        {
            AZ::Vector3 position = AZ::Vector3::CreateZero();
            AZ::TransformBus::EventResult(position, AZ::EntityId(entityId), &AZ::TransformBus::Events::GetWorldTranslation);
            xyz[0] = position.GetX();
            xyz[1] = position.GetY();
            xyz[2] = position.GetZ();
        }

        void ApiSetWorldPosition(AZ::u64 entityId, float x, float y, float z)
        {
            AZ::TransformBus::Event(AZ::EntityId(entityId), &AZ::TransformBus::Events::SetWorldTranslation, AZ::Vector3(x, y, z));
        }

        void ApiGetWorldRotationEuler(AZ::u64 entityId, float* xyzDegrees)
        {
            AZ::Quaternion rotation = AZ::Quaternion::CreateIdentity();
            AZ::TransformBus::EventResult(rotation, AZ::EntityId(entityId), &AZ::TransformBus::Events::GetWorldRotationQuaternion);
            const AZ::Vector3 euler = rotation.GetEulerDegrees();
            xyzDegrees[0] = euler.GetX();
            xyzDegrees[1] = euler.GetY();
            xyzDegrees[2] = euler.GetZ();
        }

        void ApiSetWorldRotationEuler(AZ::u64 entityId, float xDegrees, float yDegrees, float zDegrees)
        {
            const AZ::Quaternion rotation = AZ::Quaternion::CreateFromEulerDegreesXYZ(AZ::Vector3(xDegrees, yDegrees, zDegrees));
            AZ::Transform transform = AZ::Transform::CreateIdentity();
            AZ::TransformBus::EventResult(transform, AZ::EntityId(entityId), &AZ::TransformBus::Events::GetWorldTM);
            transform.SetRotation(rotation);
            AZ::TransformBus::Event(AZ::EntityId(entityId), &AZ::TransformBus::Events::SetWorldTM, transform);
        }

        float ApiGetUniformScale(AZ::u64 entityId)
        {
            float scale = 1.0f;
            AZ::TransformBus::EventResult(scale, AZ::EntityId(entityId), &AZ::TransformBus::Events::GetWorldUniformScale);
            return scale;
        }

        void ApiSetUniformScale(AZ::u64 entityId, float scale)
        {
            AZ::TransformBus::Event(AZ::EntityId(entityId), &AZ::TransformBus::Events::SetLocalUniformScale, scale);
        }

        AZ::u64 ApiFindEntityByName(const char* name)
        {
            AZ::u64 found = 0;
            AZ::ComponentApplicationBus::Broadcast(
                &AZ::ComponentApplicationBus::Events::EnumerateEntities,
                [&found, name](AZ::Entity* entity)
                {
                    if (found == 0 && entity->GetName() == name)
                    {
                        found = static_cast<AZ::u64>(entity->GetId());
                    }
                });
            return found;
        }

        void ApiGetEntityName(AZ::u64 entityId, char* buffer, int bufferSize)
        {
            AZStd::string name;
            AZ::ComponentApplicationBus::BroadcastResult(
                name, &AZ::ComponentApplicationBus::Events::GetEntityName, AZ::EntityId(entityId));
            azstrncpy(buffer, bufferSize, name.c_str(), bufferSize - 1);
            buffer[bufferSize - 1] = '\0';
        }

        bool RunCommand(const AZStd::string& command, AZStd::string& output)
        {
#if defined(_WIN32)
            FILE* pipe = _popen((command + " 2>&1").c_str(), "r");
#else
            FILE* pipe = popen((command + " 2>&1").c_str(), "r");
#endif
            if (!pipe)
            {
                return false;
            }
            char buffer[512];
            while (fgets(buffer, sizeof(buffer), pipe))
            {
                output += buffer;
            }
#if defined(_WIN32)
            const int exitCode = _pclose(pipe);
#else
            const int exitCode = pclose(pipe);
#endif
            return exitCode == 0;
        }

        AZ::u64 NewestSourceStamp(const AZStd::string& directory)
        {
            AZ::u64 newest = 0;
            AZ::IO::SystemFile::FindFiles(
                (directory + "/*").c_str(),
                [&](const char* item, bool isFile)
                {
                    if (item[0] == '.')
                    {
                        return true;
                    }
                    const AZStd::string path = directory + "/" + item;
                    if (isFile)
                    {
                        if (AZ::StringFunc::EndsWith(path, ".cs"))
                        {
                            newest = AZStd::max(newest, AZ::IO::SystemFile::ModificationTime(path.c_str()));
                        }
                    }
                    else
                    {
                        newest = AZStd::max(newest, NewestSourceStamp(path));
                    }
                    return true;
                });
            return newest;
        }
    } // namespace

    ScriptHost& ScriptHost::Instance()
    {
        static ScriptHost instance;
        return instance;
    }

    AZStd::string ScriptHost::GemManagedSourceDir() const
    {
        return AZStd::string(AZ::Utils::GetEnginePath().c_str()) + "/Gems/CSharpScripting/Managed";
    }

    AZStd::string ScriptHost::ProjectScriptsSourceDir() const
    {
        return AZStd::string(AZ::Utils::GetProjectPath().c_str()) + "/Scripts";
    }

    AZStd::string ScriptHost::ManagedWorkDir() const
    {
        return AZStd::string(AZ::Utils::GetProjectPath().c_str()) + "/user/csharp";
    }

    bool ScriptHost::BuildManagedCore()
    {
        const AZStd::string coreDll = ManagedWorkDir() + "/core/AIO3DE.Core.dll";
        const AZStd::string coreSourceDir = GemManagedSourceDir() + "/AIO3DE.Core";
        if (AZ::IO::SystemFile::Exists(coreDll.c_str()) &&
            AZ::IO::SystemFile::ModificationTime(coreDll.c_str()) >= NewestSourceStamp(coreSourceDir))
        {
            return true;
        }

        const AZStd::string dotnet = DotNetHost::FindDotNetCli();
        if (dotnet.empty())
        {
            AZ_Error("CSharpScripting", false,
                "The 'dotnet' CLI was not found. Install the .NET 8 SDK to use C# scripting.");
            return false;
        }

        AZStd::string output;
        const AZStd::string command =
            "\"" + dotnet + "\" build \"" + coreSourceDir + "/AIO3DE.Core.csproj\" -c Release -o \"" + ManagedWorkDir() + "/core\"";
        AZ_Printf("CSharpScripting", "Building managed core: %s", command.c_str());
        if (!RunCommand(command, output))
        {
            AZ_Error("CSharpScripting", false, "AIO3DE.Core build failed:\n%s", output.c_str());
            return false;
        }
        return true;
    }

    bool ScriptHost::InitializeRuntime()
    {
        if (m_bootstrapReady)
        {
            return true;
        }
        if (!BuildManagedCore())
        {
            return false;
        }

        const AZStd::string coreDir = ManagedWorkDir() + "/core";
        const AZStd::string runtimeConfig = coreDir + "/AIO3DE.Core.runtimeconfig.json";
        if (!m_host.Initialize(runtimeConfig))
        {
            return false;
        }

        const AZStd::string coreDll = coreDir + "/AIO3DE.Core.dll";
        const AZStd::string bootstrapType = "AIO3DE.Interop.Bootstrap, AIO3DE.Core";
        m_managedInitialize = reinterpret_cast<int (*)(const NativeApi*)>(m_host.GetFunction(coreDll, bootstrapType, "Initialize"));
        m_managedLoadScripts = reinterpret_cast<int (*)(const char*)>(m_host.GetFunction(coreDll, bootstrapType, "LoadScripts"));
        m_managedCreateScript =
            reinterpret_cast<AZ::s64 (*)(const char*, AZ::u64)>(m_host.GetFunction(coreDll, bootstrapType, "CreateScript"));
        m_managedScriptOnActivate = reinterpret_cast<void (*)(AZ::s64)>(m_host.GetFunction(coreDll, bootstrapType, "ScriptOnActivate"));
        m_managedScriptOnUpdate =
            reinterpret_cast<void (*)(AZ::s64, float)>(m_host.GetFunction(coreDll, bootstrapType, "ScriptOnUpdate"));
        m_managedScriptOnDeactivate =
            reinterpret_cast<void (*)(AZ::s64)>(m_host.GetFunction(coreDll, bootstrapType, "ScriptOnDeactivate"));
        m_managedDestroyScript = reinterpret_cast<void (*)(AZ::s64)>(m_host.GetFunction(coreDll, bootstrapType, "DestroyScript"));

        if (!m_managedInitialize || !m_managedLoadScripts || !m_managedCreateScript || !m_managedScriptOnActivate ||
            !m_managedScriptOnUpdate || !m_managedScriptOnDeactivate || !m_managedDestroyScript)
        {
            return false;
        }

        static const NativeApi api = {
            &ApiLog,
            &ApiGetWorldPosition,
            &ApiSetWorldPosition,
            &ApiGetWorldRotationEuler,
            &ApiSetWorldRotationEuler,
            &ApiGetUniformScale,
            &ApiSetUniformScale,
            &ApiFindEntityByName,
            &ApiGetEntityName,
        };
        if (m_managedInitialize(&api) == 0)
        {
            AZ_Error("CSharpScripting", false, "Managed bootstrap initialization failed.");
            return false;
        }

        m_bootstrapReady = true;
        return true;
    }

    bool ScriptHost::BuildProjectScripts(bool force)
    {
        const AZStd::string scriptsDir = ProjectScriptsSourceDir();
        if (!AZ::IO::SystemFile::Exists(scriptsDir.c_str()))
        {
            AZ_Warning("CSharpScripting", false,
                "No C# scripts folder found at '%s'. Create it and add .cs files deriving from AIO3DE.ScriptComponent.",
                scriptsDir.c_str());
            return false;
        }

        const AZ::u64 stamp = NewestSourceStamp(scriptsDir);
        const AZStd::string scriptsDll = ManagedWorkDir() + "/scripts/bin/ProjectScripts.dll";
        if (!force && m_scriptsLoaded && stamp == m_scriptsSourceStamp && AZ::IO::SystemFile::Exists(scriptsDll.c_str()))
        {
            return true;
        }

        const AZStd::string dotnet = DotNetHost::FindDotNetCli();
        if (dotnet.empty())
        {
            AZ_Error("CSharpScripting", false, "The 'dotnet' CLI was not found. Install the .NET 8 SDK to use C# scripting.");
            return false;
        }

        // Generated project: compiles every .cs under <project>/Scripts against the managed core.
        const AZStd::string projectDir = ManagedWorkDir() + "/scripts";
        AZ::IO::SystemFile::CreateDir(projectDir.c_str());
        const AZStd::string csproj = projectDir + "/ProjectScripts.csproj";
        const AZStd::string csprojContent =
            "<Project Sdk=\"Microsoft.NET.Sdk\">\n"
            "  <PropertyGroup>\n"
            "    <TargetFramework>net8.0</TargetFramework>\n"
            "    <AssemblyName>ProjectScripts</AssemblyName>\n"
            "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n"
            "    <Nullable>enable</Nullable>\n"
            "    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>\n"
            "  </PropertyGroup>\n"
            "  <ItemGroup>\n"
            "    <Compile Include=\"" + scriptsDir + "/**/*.cs\" />\n"
            "    <Reference Include=\"AIO3DE.Core\">\n"
            "      <HintPath>" + ManagedWorkDir() + "/core/AIO3DE.Core.dll</HintPath>\n"
            "    </Reference>\n"
            "  </ItemGroup>\n"
            "</Project>\n";
        AZ::Utils::WriteFile(csprojContent, csproj);

        AZStd::string output;
        const AZStd::string command = "\"" + dotnet + "\" build \"" + csproj + "\" -c Release -o \"" + projectDir + "/bin\"";
        AZ_Printf("CSharpScripting", "Building project scripts: %s", command.c_str());
        if (!RunCommand(command, output))
        {
            AZ_Error("CSharpScripting", false, "C# script build failed:\n%s", output.c_str());
            return false;
        }

        m_scriptsSourceStamp = stamp;
        m_scriptsLoaded = false;
        return true;
    }

    bool ScriptHost::LoadScriptsAssembly()
    {
        if (m_scriptsLoaded)
        {
            return true;
        }
        const AZStd::string scriptsDll = ManagedWorkDir() + "/scripts/bin/ProjectScripts.dll";
        if (m_managedLoadScripts(scriptsDll.c_str()) == 0)
        {
            AZ_Error("CSharpScripting", false, "Failed to load compiled scripts assembly '%s'.", scriptsDll.c_str());
            return false;
        }
        m_scriptsLoaded = true;
        return true;
    }

    bool ScriptHost::EnsureReady()
    {
        return InitializeRuntime() && BuildProjectScripts(false) && LoadScriptsAssembly();
    }

    bool ScriptHost::RebuildScripts()
    {
        return InitializeRuntime() && BuildProjectScripts(true) && LoadScriptsAssembly();
    }

    AZ::s64 ScriptHost::CreateScript(const AZStd::string& className, AZ::EntityId entityId)
    {
        if (!EnsureReady())
        {
            return 0;
        }
        const AZ::s64 handle = m_managedCreateScript(className.c_str(), static_cast<AZ::u64>(entityId));
        AZ_Error("CSharpScripting", handle != 0,
            "Could not create C# script instance '%s'. Check the class name (namespace-qualified if in a namespace) "
            "and that it derives from AIO3DE.ScriptComponent.",
            className.c_str());
        return handle;
    }

    void ScriptHost::ScriptOnActivate(AZ::s64 handle)
    {
        if (handle != 0 && m_managedScriptOnActivate)
        {
            m_managedScriptOnActivate(handle);
        }
    }

    void ScriptHost::ScriptOnUpdate(AZ::s64 handle, float deltaTime)
    {
        if (handle != 0 && m_managedScriptOnUpdate)
        {
            m_managedScriptOnUpdate(handle, deltaTime);
        }
    }

    void ScriptHost::ScriptOnDeactivate(AZ::s64 handle)
    {
        if (handle != 0 && m_managedScriptOnDeactivate)
        {
            m_managedScriptOnDeactivate(handle);
        }
    }

    void ScriptHost::DestroyScript(AZ::s64 handle)
    {
        if (handle != 0 && m_managedDestroyScript)
        {
            m_managedDestroyScript(handle);
        }
    }
} // namespace CSharpScripting
