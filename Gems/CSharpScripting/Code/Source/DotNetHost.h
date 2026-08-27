/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/std/string/string.h>

namespace CSharpScripting
{
    //! Loads the .NET runtime (CoreCLR) into the process through hostfxr and resolves
    //! [UnmanagedCallersOnly] managed entry points. hostfxr is discovered at runtime
    //! (DOTNET_ROOT or the standard install locations), so the gem builds without the
    //! .NET SDK present; hosting simply reports unavailable if dotnet is not installed.
    class DotNetHost
    {
    public:
        //! Initializes the runtime from an assembly's .runtimeconfig.json. Safe to call again once initialized.
        bool Initialize(const AZStd::string& runtimeConfigPath);

        bool IsInitialized() const { return m_loadAssemblyAndGetFunctionPointer != nullptr; }

        //! Resolves a static [UnmanagedCallersOnly] method: typeName is namespace-qualified + assembly,
        //! e.g. "AIO3DE.Bootstrap, AIO3DE.Core".
        void* GetFunction(const AZStd::string& assemblyPath, const AZStd::string& typeName, const AZStd::string& methodName);

        //! Absolute path to the dotnet executable, empty if not found.
        static AZStd::string FindDotNetCli();

    private:
        void* m_loadAssemblyAndGetFunctionPointer = nullptr;
    };
} // namespace CSharpScripting
