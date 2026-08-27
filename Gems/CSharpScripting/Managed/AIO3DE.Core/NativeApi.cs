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

        internal static string GetEntityName(ulong entityId)
        {
            byte* buffer = stackalloc byte[256];
            Api.GetEntityName(entityId, buffer, 256);
            return Marshal.PtrToStringUTF8((nint)buffer) ?? string.Empty;
        }
    }
}
