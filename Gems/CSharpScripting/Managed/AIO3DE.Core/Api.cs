/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

using AIO3DE.Interop;

namespace AIO3DE
{
    public struct Vector3
    {
        public float X;
        public float Y;
        public float Z;

        public Vector3(float x, float y, float z)
        {
            X = x;
            Y = y;
            Z = z;
        }

        public static Vector3 Zero => new(0.0f, 0.0f, 0.0f);
        public static Vector3 operator +(Vector3 a, Vector3 b) => new(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
        public static Vector3 operator -(Vector3 a, Vector3 b) => new(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
        public static Vector3 operator *(Vector3 a, float s) => new(a.X * s, a.Y * s, a.Z * s);
        public float Length() => System.MathF.Sqrt(X * X + Y * Y + Z * Z);
        public override string ToString() => $"({X}, {Y}, {Z})";
    }

    public static class Debug
    {
        public static void Log(string message) => Native.Log(0, message);
        public static void LogWarning(string message) => Native.Log(1, message);
        public static void LogError(string message) => Native.Log(2, message);
    }

    /// <summary>A handle to an engine entity with transform access.</summary>
    public readonly struct Entity
    {
        public readonly ulong Id;

        public Entity(ulong id)
        {
            Id = id;
        }

        public bool IsValid => Id != 0;

        public static Entity Find(string name) => new(Native.FindEntityByName(name));

        public string Name => Native.GetEntityName(Id);

        public unsafe Vector3 Position
        {
            get
            {
                float* xyz = stackalloc float[3];
                Native.Api.GetWorldPosition(Id, xyz);
                return new Vector3(xyz[0], xyz[1], xyz[2]);
            }
            set => Native.Api.SetWorldPosition(Id, value.X, value.Y, value.Z);
        }

        /// <summary>World rotation as XYZ Euler angles in degrees.</summary>
        public unsafe Vector3 RotationEuler
        {
            get
            {
                float* xyz = stackalloc float[3];
                Native.Api.GetWorldRotationEuler(Id, xyz);
                return new Vector3(xyz[0], xyz[1], xyz[2]);
            }
            set => Native.Api.SetWorldRotationEuler(Id, value.X, value.Y, value.Z);
        }

        public unsafe float UniformScale
        {
            get => Native.Api.GetUniformScale(Id);
            set => Native.Api.SetUniformScale(Id, value);
        }
    }

    /// <summary>
    /// Base class for C# behaviours. Derive from it in a .cs file under &lt;project&gt;/Scripts,
    /// then set the class name on a C# Script component.
    /// </summary>
    public abstract class ScriptComponent
    {
        /// <summary>The entity this script is attached to.</summary>
        public Entity Entity;

        public virtual void OnActivate()
        {
        }

        public virtual void OnUpdate(float deltaTime)
        {
        }

        public virtual void OnDeactivate()
        {
        }
    }
}
