/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.InteropServices;

namespace AIO3DE.Interop
{
    /// <summary>Entry points the native ScriptHost resolves and calls.</summary>
    public static unsafe class Bootstrap
    {
        private static Assembly? s_scriptsAssembly;
        private static string? s_scriptsDirectory;
        private static bool s_resolverInstalled;
        private static readonly Dictionary<long, ScriptComponent> s_instances = new();
        private static long s_nextHandle = 1;

        private static Assembly? ResolveAssembly(object? sender, ResolveEventArgs args)
        {
            var requested = new AssemblyName(args.Name);
            // Byte-loaded scripts can't see assemblies loaded by path: redirect
            // AIO3DE.Core to this assembly and search next to the scripts dll otherwise.
            if (requested.Name == typeof(Bootstrap).Assembly.GetName().Name)
            {
                return typeof(Bootstrap).Assembly;
            }
            if (s_scriptsDirectory != null)
            {
                string candidate = System.IO.Path.Combine(s_scriptsDirectory, requested.Name + ".dll");
                if (System.IO.File.Exists(candidate))
                {
                    return Assembly.LoadFrom(candidate);
                }
            }
            return null;
        }

        [UnmanagedCallersOnly]
        public static int Initialize(NativeApi* api)
        {
            Native.Api = *api;
            return 1;
        }

        [UnmanagedCallersOnly]
        public static int LoadScripts(byte* assemblyPath)
        {
            try
            {
                string? path = Marshal.PtrToStringUTF8((nint)assemblyPath);
                if (string.IsNullOrEmpty(path))
                {
                    return 0;
                }
                if (!s_resolverInstalled)
                {
                    AppDomain.CurrentDomain.AssemblyResolve += ResolveAssembly;
                    s_resolverInstalled = true;
                }
                s_scriptsDirectory = System.IO.Path.GetDirectoryName(path);
                // Load bytes so the file on disk stays writable for rebuilds.
                byte[] bytes = System.IO.File.ReadAllBytes(path);
                s_scriptsAssembly = Assembly.Load(bytes);
                return 1;
            }
            catch (Exception e)
            {
                Native.Log(2, $"Failed to load scripts assembly: {e.Message}");
                return 0;
            }
        }

        [UnmanagedCallersOnly]
        public static long CreateScript(byte* className, ulong entityId)
        {
            try
            {
                string? name = Marshal.PtrToStringUTF8((nint)className);
                if (string.IsNullOrEmpty(name))
                {
                    return 0;
                }

                Type? type = FindType(name);
                if (type == null)
                {
                    Native.Log(2, $"C# script class '{name}' not found (it must derive from AIO3DE.ScriptComponent).");
                    return 0;
                }
                if (!typeof(ScriptComponent).IsAssignableFrom(type))
                {
                    Native.Log(2, $"C# class '{name}' does not derive from AIO3DE.ScriptComponent.");
                    return 0;
                }

                var instance = (ScriptComponent?)Activator.CreateInstance(type);
                if (instance == null)
                {
                    return 0;
                }
                instance.Entity = new Entity(entityId);

                long handle = s_nextHandle++;
                s_instances[handle] = instance;
                return handle;
            }
            catch (Exception e)
            {
                Native.Log(2, $"Failed to create C# script: {e}");
                return 0;
            }
        }

        [UnmanagedCallersOnly]
        public static void ScriptOnActivate(long handle)
        {
            if (s_instances.TryGetValue(handle, out var script))
            {
                Guarded(() => script.OnActivate(), script, "OnActivate");
            }
        }

        [UnmanagedCallersOnly]
        public static void ScriptOnUpdate(long handle, float deltaTime)
        {
            if (s_instances.TryGetValue(handle, out var script))
            {
                try
                {
                    script.OnUpdate(deltaTime);
                }
                catch (Exception e)
                {
                    Native.Log(2, $"{script.GetType().Name}.OnUpdate threw: {e}");
                }
            }
        }

        [UnmanagedCallersOnly]
        public static void ScriptOnDeactivate(long handle)
        {
            if (s_instances.TryGetValue(handle, out var script))
            {
                Guarded(() => script.OnDeactivate(), script, "OnDeactivate");
            }
        }

        [UnmanagedCallersOnly]
        public static void DestroyScript(long handle)
        {
            s_instances.Remove(handle);
        }

        private static void Guarded(Action action, ScriptComponent script, string phase)
        {
            try
            {
                action();
            }
            catch (Exception e)
            {
                Native.Log(2, $"{script.GetType().Name}.{phase} threw: {e}");
            }
        }

        private static Type? FindType(string name)
        {
            if (s_scriptsAssembly != null)
            {
                Type? type = s_scriptsAssembly.GetType(name, throwOnError: false);
                if (type != null)
                {
                    return type;
                }
                // Allow bare class names for classes declared in a namespace.
                foreach (Type candidate in s_scriptsAssembly.GetTypes())
                {
                    if (candidate.Name == name)
                    {
                        return candidate;
                    }
                }
            }
            return null;
        }
    }
}
