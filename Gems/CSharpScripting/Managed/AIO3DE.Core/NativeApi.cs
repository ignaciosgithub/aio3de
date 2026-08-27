/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

using System.Runtime.InteropServices;

namespace AIO3DE.Interop
{
    /// <summary>
    /// Function pointers into the engine. Field order is ABI: it must match
    /// CSharpScripting::NativeApi in ScriptHost.h.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct NativeApi
    {
        public delegate* unmanaged<int, byte*, void> Log;
        public delegate* unmanaged<ulong, float*, void> GetWorldPosition;
        public delegate* unmanaged<ulong, float, float, float, void> SetWorldPosition;
        public delegate* unmanaged<ulong, float*, void> GetWorldRotationEuler;
        public delegate* unmanaged<ulong, float, float, float, void> SetWorldRotationEuler;
        public delegate* unmanaged<ulong, float> GetUniformScale;
        public delegate* unmanaged<ulong, float, void> SetUniformScale;
        public delegate* unmanaged<byte*, ulong> FindEntityByName;
        public delegate* unmanaged<ulong, byte*, int, void> GetEntityName;

        // Transform extras
        public delegate* unmanaged<ulong, float*, void> GetLocalPosition;
        public delegate* unmanaged<ulong, float, float, float, void> SetLocalPosition;
        public delegate* unmanaged<ulong, float*, void> GetWorldRotationQuaternion;
        public delegate* unmanaged<ulong, float, float, float, float, void> SetWorldRotationQuaternion;
        public delegate* unmanaged<ulong, float*, void> GetWorldBasis;
        public delegate* unmanaged<ulong, ulong, void> SetParent;
        public delegate* unmanaged<ulong, ulong> GetParent;

        // Entity lifecycle
        public delegate* unmanaged<byte*, ulong> CreateEntity;
        public delegate* unmanaged<ulong, void> DestroyEntity;
        public delegate* unmanaged<ulong, int, void> SetEntityActive;
        public delegate* unmanaged<ulong, int> IsEntityActive;

        // Input
        public delegate* unmanaged<byte*, int> IsChannelActive;
        public delegate* unmanaged<byte*, float> GetChannelValue;
        public delegate* unmanaged<float*, void> GetCursorPositionNormalized;

        // Time
        public delegate* unmanaged<double> GetTimeSeconds;

        // Physics
        public delegate* unmanaged<float, float, float, float, float, float, float, float*, float*, ulong*, int> RayCast;
        public delegate* unmanaged<ulong, float*, void> GetLinearVelocity;
        public delegate* unmanaged<ulong, float, float, float, void> SetLinearVelocity;
        public delegate* unmanaged<ulong, float*, void> GetAngularVelocity;
        public delegate* unmanaged<ulong, float, float, float, void> SetAngularVelocity;
        public delegate* unmanaged<ulong, float, float, float, void> ApplyLinearImpulse;
        public delegate* unmanaged<ulong, float, float, float, void> ApplyAngularImpulse;
        public delegate* unmanaged<ulong, float> GetMass;
        public delegate* unmanaged<ulong, int, void> SetGravityEnabled;
        public delegate* unmanaged<ulong, int, void> SetKinematic;
    }

    internal static unsafe class Native
    {
        internal static NativeApi Api;

        internal static void Log(int level, string message)
        {
            byte[] utf8 = System.Text.Encoding.UTF8.GetBytes(message + "\0");
            fixed (byte* p = utf8)
            {
                Api.Log(level, p);
            }
        }

        internal static ulong FindEntityByName(string name)
        {
            byte[] utf8 = System.Text.Encoding.UTF8.GetBytes(name + "\0");
            fixed (byte* p = utf8)
            {
                return Api.FindEntityByName(p);
            }
        }

        internal static bool IsChannelActive(string channelName)
        {
            byte[] utf8 = System.Text.Encoding.UTF8.GetBytes(channelName + "\0");
            fixed (byte* p = utf8)
            {
                return Api.IsChannelActive(p) != 0;
            }
        }

        internal static float GetChannelValue(string channelName)
        {
            byte[] utf8 = System.Text.Encoding.UTF8.GetBytes(channelName + "\0");
            fixed (byte* p = utf8)
            {
                return Api.GetChannelValue(p);
            }
        }

        internal static ulong CreateEntity(string name)
        {
            byte[] utf8 = System.Text.Encoding.UTF8.GetBytes(name + "\0");
            fixed (byte* p = utf8)
            {
                return Api.CreateEntity(p);
            }
        }

        internal static string GetEntityName(ulong entityId)
        {
            byte* buffer = stackalloc byte[256];
            Api.GetEntityName(entityId, buffer, 256);
            return Marshal.PtrToStringUTF8((nint)buffer) ?? string.Empty;
        }
    }
}
