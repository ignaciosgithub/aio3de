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
using System.Runtime.Loader;

namespace AIO3DE.Interop
{
    /// <summary>Entry points the native ScriptHost resolves and calls.</summary>
    public static unsafe class Bootstrap
    {
        private static Assembly? s_scriptsAssembly;
        private static AssemblyLoadContext? s_scriptsContext;
        private static string? s_scriptsDirectory;
        private static readonly Dictionary<long, ScriptComponent> s_instances = new();
        private static long s_nextHandle = 1;

        /// <summary>
        /// Collectible context per scripts load: on hot reload the previous context is
        /// unloaded so old assemblies and types can be collected once instances are gone.
        /// </summary>
        private sealed class ScriptsLoadContext : AssemblyLoadContext
        {
            private readonly string? _scriptsDirectory;

            public ScriptsLoadContext(string? scriptsDirectory)
                : base("AIO3DE.Scripts", isCollectible: true)
            {
                _scriptsDirectory = scriptsDirectory;
            }

            protected override Assembly? Load(AssemblyName assemblyName)
            {
                // Share AIO3DE.Core (and everything else already loaded) with the default
                // context so ScriptComponent type identity stays stable across reloads.
                if (assemblyName.Name == typeof(Bootstrap).Assembly.GetName().Name)
                {
                    return typeof(Bootstrap).Assembly;
                }
                if (_scriptsDirectory != null)
                {
                    string candidate = System.IO.Path.Combine(_scriptsDirectory, assemblyName.Name + ".dll");
                    if (System.IO.File.Exists(candidate))
                    {
                        return LoadFromAssemblyPath(candidate);
                    }
                }
                return null;
            }
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

                // Hot reload: drop live instances tied to the old assembly, then unload
                // its collectible context so the old types can be collected.
                if (s_scriptsContext != null)
                {
                    foreach (var script in s_instances.Values)
                    {
                        Guarded(() => script.OnDeactivate(), script, "OnDeactivate");
                    }
                    s_instances.Clear();
                    s_scriptsAssembly = null;
                    s_scriptsContext.Unload();
                    s_scriptsContext = null;
                }

                s_scriptsDirectory = System.IO.Path.GetDirectoryName(path);
                var context = new ScriptsLoadContext(s_scriptsDirectory);
                // Load from a stream so the file on disk stays writable for rebuilds.
                using (var stream = System.IO.File.OpenRead(path))
                {
                    s_scriptsAssembly = context.LoadFromStream(stream);
                }
                s_scriptsContext = context;
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

        [UnmanagedCallersOnly]
        public static void ScriptOnCollisionEnter(
            long handle, ulong otherEntityId,
            float positionX, float positionY, float positionZ,
            float normalX, float normalY, float normalZ, float impulse)
        {
            if (s_instances.TryGetValue(handle, out var script))
            {
                var collision = new Collision
                {
                    Other = new Entity(otherEntityId),
                    Position = new Vector3(positionX, positionY, positionZ),
                    Normal = new Vector3(normalX, normalY, normalZ),
                    Impulse = impulse,
                };
                Guarded(() => script.OnCollisionEnter(collision), script, "OnCollisionEnter");
            }
        }

        [UnmanagedCallersOnly]
        public static void ScriptOnCollisionExit(long handle, ulong otherEntityId)
        {
            if (s_instances.TryGetValue(handle, out var script))
            {
                Guarded(() => script.OnCollisionExit(new Entity(otherEntityId)), script, "OnCollisionExit");
            }
        }

        [UnmanagedCallersOnly]
        public static void ScriptOnTriggerEnter(long handle, ulong otherEntityId)
        {
            if (s_instances.TryGetValue(handle, out var script))
            {
                Guarded(() => script.OnTriggerEnter(new Entity(otherEntityId)), script, "OnTriggerEnter");
            }
        }

        [UnmanagedCallersOnly]
        public static void ScriptOnTriggerExit(long handle, ulong otherEntityId)
        {
            if (s_instances.TryGetValue(handle, out var script))
            {
                Guarded(() => script.OnTriggerExit(new Entity(otherEntityId)), script, "OnTriggerExit");
            }
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
