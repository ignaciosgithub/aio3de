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
        private static readonly HashSet<long> s_disabled = new();
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

        /// <summary>
        /// Writes the inspectable fields of a script class (with default values) into the
        /// buffer as records "name\x1ftype\x1fvalue" separated by '\x1e'.
        /// Returns the written byte count, or -1 on failure / too-small buffer.
        /// </summary>
        [UnmanagedCallersOnly]
        public static int DescribeScript(byte* className, byte* buffer, int bufferSize)
        {
            try
            {
                string? name = Marshal.PtrToStringUTF8((nint)className);
                if (string.IsNullOrEmpty(name))
                {
                    return -1;
                }
                Type? type = FindType(name);
                if (type == null || !typeof(ScriptComponent).IsAssignableFrom(type))
                {
                    return -1;
                }

                object? defaults = null;
                try
                {
                    defaults = Activator.CreateInstance(type);
                }
                catch
                {
                    // No usable defaults; report zeros.
                }

                var records = new List<string>();
                foreach (FieldInfo field in EnumerateInspectableFields(type))
                {
                    string? typeName = FieldTypeName(field.FieldType);
                    if (typeName == null)
                    {
                        continue;
                    }
                    object? value = defaults != null ? field.GetValue(defaults) : null;
                    records.Add($"{field.Name}\x1f{typeName}\x1f{FieldValueToString(field.FieldType, value)}");
                }

                byte[] bytes = System.Text.Encoding.UTF8.GetBytes(string.Join('\x1e', records));
                if (bytes.Length > bufferSize)
                {
                    return -1;
                }
                Marshal.Copy(bytes, 0, (nint)buffer, bytes.Length);
                return bytes.Length;
            }
            catch (Exception e)
            {
                Native.Log(2, $"DescribeScript failed: {e.Message}");
                return -1;
            }
        }

        /// <summary>Sets a field on a live script instance from its string form. Returns 1 on success.</summary>
        [UnmanagedCallersOnly]
        public static int SetScriptField(long handle, byte* fieldName, byte* value)
        {
            try
            {
                if (!s_instances.TryGetValue(handle, out var script))
                {
                    return 0;
                }
                string? name = Marshal.PtrToStringUTF8((nint)fieldName);
                string? text = Marshal.PtrToStringUTF8((nint)value);
                if (string.IsNullOrEmpty(name) || text == null)
                {
                    return 0;
                }
                FieldInfo? field = script.GetType().GetField(
                    name, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
                if (field == null || FieldTypeName(field.FieldType) == null || !IsInspectable(field))
                {
                    return 0;
                }
                field.SetValue(script, ParseFieldValue(field.FieldType, text));
                return 1;
            }
            catch (Exception e)
            {
                Native.Log(2, $"SetScriptField failed: {e.Message}");
                return 0;
            }
        }

        private static bool IsInspectable(FieldInfo field)
        {
            if (field.IsInitOnly || field.IsDefined(typeof(HideInInspector), inherit: true))
            {
                return false;
            }
            // The ScriptComponent.Entity field is assigned by the engine, not the Inspector.
            if (field.DeclaringType == typeof(ScriptComponent))
            {
                return false;
            }
            return field.IsPublic || field.IsDefined(typeof(SerializeField), inherit: true);
        }

        private static IEnumerable<FieldInfo> EnumerateInspectableFields(Type type)
        {
            foreach (FieldInfo field in type.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic))
            {
                if (IsInspectable(field))
                {
                    yield return field;
                }
            }
        }

        private static string? FieldTypeName(Type type)
        {
            if (type == typeof(float)) return "float";
            if (type == typeof(int)) return "int";
            if (type == typeof(bool)) return "bool";
            if (type == typeof(string)) return "string";
            if (type == typeof(Vector3)) return "vector3";
            if (type == typeof(Entity)) return "entity";
            return null;
        }

        private static string FieldValueToString(Type type, object? value)
        {
            var culture = System.Globalization.CultureInfo.InvariantCulture;
            if (type == typeof(float)) return ((float)(value ?? 0.0f)).ToString("R", culture);
            if (type == typeof(int)) return ((int)(value ?? 0)).ToString(culture);
            if (type == typeof(bool)) return ((bool)(value ?? false)) ? "true" : "false";
            if (type == typeof(string)) return (string?)value ?? string.Empty;
            if (type == typeof(Entity)) return ((Entity)(value ?? default(Entity))).Id.ToString(culture);
            var v = (Vector3)(value ?? Vector3.Zero);
            return string.Create(culture, $"{v.X:R} {v.Y:R} {v.Z:R}");
        }

        private static object ParseFieldValue(Type type, string text)
        {
            var culture = System.Globalization.CultureInfo.InvariantCulture;
            if (type == typeof(float)) return float.Parse(text, culture);
            if (type == typeof(int)) return (int)long.Parse(text, culture);
            if (type == typeof(bool)) return text == "true" || text == "True" || text == "1";
            if (type == typeof(string)) return text;
            if (type == typeof(Entity)) return new Entity(ulong.Parse(text, culture));
            string[] parts = text.Split(' ', StringSplitOptions.RemoveEmptyEntries);
            return new Vector3(
                float.Parse(parts[0], culture),
                float.Parse(parts[1], culture),
                float.Parse(parts[2], culture));
        }

        [UnmanagedCallersOnly]
        public static void ScriptOnActivate(long handle)
        {
            if (s_instances.TryGetValue(handle, out var script))
            {
                if (!RequirementsMet(script))
                {
                    s_disabled.Add(handle);
                    return;
                }
                s_disabled.Remove(handle);
                Guarded(() => script.OnActivate(), script, "OnActivate");
            }
        }

        private static bool RequirementsMet(ScriptComponent script)
        {
            foreach (RequireComponent requirement in script.GetType().GetCustomAttributes<RequireComponent>(inherit: true))
            {
                if (!script.Entity.HasComponent(requirement.TypeName))
                {
                    Native.Log(1,
                        $"{script.GetType().Name} on entity '{script.Entity.Name}' is disabled: " +
                        $"required component '{requirement.TypeName}' is missing.");
                    return false;
                }
            }
            return true;
        }

        [UnmanagedCallersOnly]
        public static void ScriptOnUpdate(long handle, float deltaTime)
        {
            if (s_instances.TryGetValue(handle, out var script) && !s_disabled.Contains(handle))
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
            if (s_instances.TryGetValue(handle, out var script) && !s_disabled.Contains(handle))
            {
                Guarded(() => script.OnDeactivate(), script, "OnDeactivate");
            }
        }

        [UnmanagedCallersOnly]
        public static void DestroyScript(long handle)
        {
            s_instances.Remove(handle);
            s_disabled.Remove(handle);
        }

        [UnmanagedCallersOnly]
        public static void ScriptOnCollisionEnter(
            long handle, ulong otherEntityId,
            float positionX, float positionY, float positionZ,
            float normalX, float normalY, float normalZ, float impulse)
        {
            if (s_instances.TryGetValue(handle, out var script) && !s_disabled.Contains(handle))
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
            if (s_instances.TryGetValue(handle, out var script) && !s_disabled.Contains(handle))
            {
                Guarded(() => script.OnCollisionExit(new Entity(otherEntityId)), script, "OnCollisionExit");
            }
        }

        [UnmanagedCallersOnly]
        public static void ScriptOnTriggerEnter(long handle, ulong otherEntityId)
        {
            if (s_instances.TryGetValue(handle, out var script) && !s_disabled.Contains(handle))
            {
                Guarded(() => script.OnTriggerEnter(new Entity(otherEntityId)), script, "OnTriggerEnter");
            }
        }

        [UnmanagedCallersOnly]
        public static void ScriptOnTriggerExit(long handle, ulong otherEntityId)
        {
            if (s_instances.TryGetValue(handle, out var script) && !s_disabled.Contains(handle))
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
