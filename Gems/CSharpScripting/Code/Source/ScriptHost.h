/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include "DotNetHost.h"

#include <AzCore/Component/EntityId.h>
#include <AzCore/std/string/string.h>

namespace CSharpScripting
{
    //! Native functions handed to the managed side (AIO3DE.Core). Field order is ABI: it must
    //! match AIO3DE.Interop.NativeApi in NativeApi.cs.
    struct NativeApi
    {
        void (*m_log)(int level, const char* message);
        void (*m_getWorldPosition)(AZ::u64 entityId, float* xyz);
        void (*m_setWorldPosition)(AZ::u64 entityId, float x, float y, float z);
        void (*m_getWorldRotationEuler)(AZ::u64 entityId, float* xyzDegrees);
        void (*m_setWorldRotationEuler)(AZ::u64 entityId, float xDegrees, float yDegrees, float zDegrees);
        float (*m_getUniformScale)(AZ::u64 entityId);
        void (*m_setUniformScale)(AZ::u64 entityId, float scale);
        AZ::u64 (*m_findEntityByName)(const char* name);
        void (*m_getEntityName)(AZ::u64 entityId, char* buffer, int bufferSize);
    };

    //! Owns the .NET runtime, compiles the managed core + project scripts with the dotnet CLI,
    //! and dispatches script lifecycle calls into AIO3DE.Core's Bootstrap entry points.
    class ScriptHost
    {
    public:
        static ScriptHost& Instance();

        //! Compiles (if out of date) and loads the managed core + <project>/Scripts. Idempotent per build.
        bool EnsureReady();

        //! Forces a rebuild of the project scripts and reloads them (hot reload).
        bool RebuildScripts();

        AZ::s64 CreateScript(const AZStd::string& className, AZ::EntityId entityId);
        void ScriptOnActivate(AZ::s64 handle);
        void ScriptOnUpdate(AZ::s64 handle, float deltaTime);
        void ScriptOnDeactivate(AZ::s64 handle);
        void DestroyScript(AZ::s64 handle);

    private:
        bool InitializeRuntime();
        bool BuildManagedCore();
        bool BuildProjectScripts(bool force);
        bool LoadScriptsAssembly();

        AZStd::string ProjectScriptsSourceDir() const;
        AZStd::string ManagedWorkDir() const;
        AZStd::string GemManagedSourceDir() const;

        DotNetHost m_host;
        bool m_bootstrapReady = false;
        bool m_scriptsLoaded = false;
        AZ::u64 m_scriptsSourceStamp = 0;

        // Managed entry points (AIO3DE.Bootstrap, AIO3DE.Core)
        int (*m_managedInitialize)(const NativeApi* api) = nullptr;
        int (*m_managedLoadScripts)(const char* assemblyPath) = nullptr;
        AZ::s64 (*m_managedCreateScript)(const char* className, AZ::u64 entityId) = nullptr;
        void (*m_managedScriptOnActivate)(AZ::s64 handle) = nullptr;
        void (*m_managedScriptOnUpdate)(AZ::s64 handle, float deltaTime) = nullptr;
        void (*m_managedScriptOnDeactivate)(AZ::s64 handle) = nullptr;
        void (*m_managedDestroyScript)(AZ::s64 handle) = nullptr;
    };
} // namespace CSharpScripting
