/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "DotNetHost.h"

#include <AzCore/Debug/Trace.h>
#include <AzCore/IO/SystemFile.h>
#include <AzCore/std/containers/fixed_vector.h>
#include <AzCore/std/string/conversions.h>

#include <cstdlib>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

namespace CSharpScripting
{
    // Minimal hostfxr declarations (stable, documented native hosting ABI) so the gem
    // builds without .NET SDK headers; the library itself is loaded dynamically.
    namespace HostFxr
    {
#if defined(_WIN32)
        using char_t = wchar_t;
#else
        using char_t = char;
#endif
        using hostfxr_handle = void*;

        struct hostfxr_initialize_parameters;

        using hostfxr_initialize_for_runtime_config_fn =
            int (*)(const char_t* runtimeConfigPath, const hostfxr_initialize_parameters* parameters, hostfxr_handle* hostContextHandle);
        using hostfxr_get_runtime_delegate_fn = int (*)(hostfxr_handle hostContextHandle, int type, void** delegate);
        using hostfxr_close_fn = int (*)(hostfxr_handle hostContextHandle);

        constexpr int hdt_load_assembly_and_get_function_pointer = 5;

        using load_assembly_and_get_function_pointer_fn = int (*)(
            const char_t* assemblyPath,
            const char_t* typeName,
            const char_t* methodName,
            const char_t* delegateTypeName,
            void* reserved,
            void** delegate);

        // UNMANAGEDCALLERSONLY_METHOD sentinel
        inline const char_t* UnmanagedCallersOnlyMethod()
        {
            return reinterpret_cast<const char_t*>(-1);
        }
    } // namespace HostFxr

    namespace
    {
        void* LoadLibraryPortable(const char* path)
        {
#if defined(_WIN32)
            return ::LoadLibraryA(path);
#else
            return ::dlopen(path, RTLD_NOW | RTLD_GLOBAL);
#endif
        }

        void* GetSymbolPortable(void* handle, const char* name)
        {
#if defined(_WIN32)
            return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(handle), name));
#else
            return ::dlsym(handle, name);
#endif
        }

#if defined(_WIN32)
        AZStd::wstring ToHostString(const AZStd::string& value)
        {
            AZStd::wstring result;
            AZStd::to_wstring(result, value);
            return result;
        }
#else
        const AZStd::string& ToHostString(const AZStd::string& value)
        {
            return value;
        }
#endif

        AZStd::string FindNewestSubdirEntry(const AZStd::string& fxrDir, const char* libraryName)
        {
            // Pick the lexicographically-newest version folder under host/fxr.
            AZStd::string best;
            AZStd::string bestVersion;
            AZ::IO::SystemFile::FindFiles(
                (fxrDir + "/*").c_str(),
                [&](const char* item, bool isFile)
                {
                    if (!isFile && item[0] != '.')
                    {
                        if (AZStd::string(item) > bestVersion)
                        {
                            AZStd::string candidate = fxrDir + "/" + item + "/" + libraryName;
                            if (AZ::IO::SystemFile::Exists(candidate.c_str()))
                            {
                                bestVersion = item;
                                best = candidate;
                            }
                        }
                    }
                    return true;
                });
            return best;
        }

        AZStd::string FindHostFxrLibrary()
        {
#if defined(_WIN32)
            const char* libraryName = "hostfxr.dll";
#elif defined(__APPLE__)
            const char* libraryName = "libhostfxr.dylib";
#else
            const char* libraryName = "libhostfxr.so";
#endif
            AZStd::fixed_vector<AZStd::string, 4> roots;
            if (const char* dotnetRoot = ::getenv("DOTNET_ROOT"); dotnetRoot && dotnetRoot[0])
            {
                roots.push_back(dotnetRoot);
            }
#if defined(_WIN32)
            roots.push_back("C:\\Program Files\\dotnet");
#else
            roots.push_back("/usr/lib/dotnet");
            roots.push_back("/usr/share/dotnet");
            roots.push_back("/usr/local/share/dotnet");
#endif
            for (const AZStd::string& root : roots)
            {
                AZStd::string result = FindNewestSubdirEntry(root + "/host/fxr", libraryName);
                if (!result.empty())
                {
                    return result;
                }
            }
            return {};
        }
    } // namespace

    AZStd::string DotNetHost::FindDotNetCli()
    {
#if defined(_WIN32)
        const char* cliName = "dotnet.exe";
#else
        const char* cliName = "dotnet";
#endif
        AZStd::fixed_vector<AZStd::string, 4> roots;
        if (const char* dotnetRoot = ::getenv("DOTNET_ROOT"); dotnetRoot && dotnetRoot[0])
        {
            roots.push_back(dotnetRoot);
        }
#if defined(_WIN32)
        roots.push_back("C:\\Program Files\\dotnet");
#else
        roots.push_back("/usr/lib/dotnet");
        roots.push_back("/usr/share/dotnet");
        roots.push_back("/usr/local/share/dotnet");
        roots.push_back("/usr/bin");
#endif
        for (const AZStd::string& root : roots)
        {
            AZStd::string candidate = root + "/" + cliName;
            if (AZ::IO::SystemFile::Exists(candidate.c_str()))
            {
                return candidate;
            }
        }
        return {};
    }

    bool DotNetHost::Initialize(const AZStd::string& runtimeConfigPath)
    {
        if (IsInitialized())
        {
            return true;
        }

        const AZStd::string libraryPath = FindHostFxrLibrary();
        if (libraryPath.empty())
        {
            AZ_Error("CSharpScripting", false,
                "Could not find the .NET runtime (hostfxr). Install the .NET 8 SDK (https://dotnet.microsoft.com/download) "
                "or set DOTNET_ROOT to your dotnet install directory.");
            return false;
        }

        void* library = LoadLibraryPortable(libraryPath.c_str());
        if (!library)
        {
            AZ_Error("CSharpScripting", false, "Failed to load hostfxr from '%s'.", libraryPath.c_str());
            return false;
        }

        auto initialize = reinterpret_cast<HostFxr::hostfxr_initialize_for_runtime_config_fn>(
            GetSymbolPortable(library, "hostfxr_initialize_for_runtime_config"));
        auto getDelegate = reinterpret_cast<HostFxr::hostfxr_get_runtime_delegate_fn>(
            GetSymbolPortable(library, "hostfxr_get_runtime_delegate"));
        auto close = reinterpret_cast<HostFxr::hostfxr_close_fn>(GetSymbolPortable(library, "hostfxr_close"));
        if (!initialize || !getDelegate || !close)
        {
            AZ_Error("CSharpScripting", false, "hostfxr at '%s' is missing required entry points.", libraryPath.c_str());
            return false;
        }

        const auto hostConfigPath = ToHostString(runtimeConfigPath);
        HostFxr::hostfxr_handle context = nullptr;
        const int initResult = initialize(hostConfigPath.c_str(), nullptr, &context);
        if (initResult < 0 || !context) // 0 = success, 1..2 = already initialized (compatible)
        {
            AZ_Error("CSharpScripting", false,
                "hostfxr_initialize_for_runtime_config('%s') failed with 0x%08x.", runtimeConfigPath.c_str(), initResult);
            if (context)
            {
                close(context);
            }
            return false;
        }

        void* delegate = nullptr;
        const int delegateResult = getDelegate(context, HostFxr::hdt_load_assembly_and_get_function_pointer, &delegate);
        close(context);
        if (delegateResult != 0 || !delegate)
        {
            AZ_Error("CSharpScripting", false, "hostfxr_get_runtime_delegate failed with 0x%08x.", delegateResult);
            return false;
        }

        m_loadAssemblyAndGetFunctionPointer = delegate;
        return true;
    }

    void* DotNetHost::GetFunction(
        const AZStd::string& assemblyPath, const AZStd::string& typeName, const AZStd::string& methodName)
    {
        if (!IsInitialized())
        {
            return nullptr;
        }

        auto loader = reinterpret_cast<HostFxr::load_assembly_and_get_function_pointer_fn>(m_loadAssemblyAndGetFunctionPointer);
        const auto hostAssemblyPath = ToHostString(assemblyPath);
        const auto hostTypeName = ToHostString(typeName);
        const auto hostMethodName = ToHostString(methodName);

        void* function = nullptr;
        const int result = loader(
            hostAssemblyPath.c_str(),
            hostTypeName.c_str(),
            hostMethodName.c_str(),
            HostFxr::UnmanagedCallersOnlyMethod(),
            nullptr,
            &function);
        if (result != 0 || !function)
        {
            AZ_Error("CSharpScripting", false,
                "Failed to resolve managed method %s.%s in '%s' (0x%08x).",
                typeName.c_str(), methodName.c_str(), assemblyPath.c_str(), result);
            return nullptr;
        }
        return function;
    }
} // namespace CSharpScripting
