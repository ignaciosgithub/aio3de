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
        public static Vector3 One => new(1.0f, 1.0f, 1.0f);
        public static Vector3 Up => new(0.0f, 0.0f, 1.0f);
        public static Vector3 Forward => new(0.0f, 1.0f, 0.0f);
        public static Vector3 Right => new(1.0f, 0.0f, 0.0f);

        public static Vector3 operator +(Vector3 a, Vector3 b) => new(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
        public static Vector3 operator -(Vector3 a, Vector3 b) => new(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
        public static Vector3 operator -(Vector3 a) => new(-a.X, -a.Y, -a.Z);
        public static Vector3 operator *(Vector3 a, float s) => new(a.X * s, a.Y * s, a.Z * s);
        public static Vector3 operator *(float s, Vector3 a) => a * s;
        public static Vector3 operator /(Vector3 a, float s) => new(a.X / s, a.Y / s, a.Z / s);

        public float Length() => System.MathF.Sqrt(X * X + Y * Y + Z * Z);
        public float LengthSquared() => X * X + Y * Y + Z * Z;

        public Vector3 Normalized()
        {
            float length = Length();
            return length > 1e-6f ? this / length : Zero;
        }

        public static float Dot(Vector3 a, Vector3 b) => a.X * b.X + a.Y * b.Y + a.Z * b.Z;

        public static Vector3 Cross(Vector3 a, Vector3 b) =>
            new(a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X);

        public static float Distance(Vector3 a, Vector3 b) => (a - b).Length();

        public static Vector3 Lerp(Vector3 a, Vector3 b, float t) => a + (b - a) * t;

        public override string ToString() => $"({X}, {Y}, {Z})";
    }

    /// <summary>Rotation quaternion (X, Y, Z, W).</summary>
    public struct Quaternion
    {
        public float X;
        public float Y;
        public float Z;
        public float W;

        public Quaternion(float x, float y, float z, float w)
        {
            X = x;
            Y = y;
            Z = z;
            W = w;
        }

        public static Quaternion Identity => new(0.0f, 0.0f, 0.0f, 1.0f);

        public static Quaternion FromAxisAngle(Vector3 axis, float angleDegrees)
        {
            Vector3 normalized = axis.Normalized();
            float half = angleDegrees * (System.MathF.PI / 180.0f) * 0.5f;
            float sin = System.MathF.Sin(half);
            return new Quaternion(normalized.X * sin, normalized.Y * sin, normalized.Z * sin, System.MathF.Cos(half));
        }

        public static Quaternion operator *(Quaternion a, Quaternion b) => new(
            a.W * b.X + a.X * b.W + a.Y * b.Z - a.Z * b.Y,
            a.W * b.Y - a.X * b.Z + a.Y * b.W + a.Z * b.X,
            a.W * b.Z + a.X * b.Y - a.Y * b.X + a.Z * b.W,
            a.W * b.W - a.X * b.X - a.Y * b.Y - a.Z * b.Z);

        public Vector3 Rotate(Vector3 v)
        {
            Vector3 q = new(X, Y, Z);
            Vector3 t = Vector3.Cross(q, v) * 2.0f;
            return v + t * W + Vector3.Cross(q, t);
        }

        public Quaternion Normalized()
        {
            float length = System.MathF.Sqrt(X * X + Y * Y + Z * Z + W * W);
            return length > 1e-6f ? new Quaternion(X / length, Y / length, Z / length, W / length) : Identity;
        }

        public override string ToString() => $"({X}, {Y}, {Z}, {W})";
    }

    public static class Debug
    {
        public static void Log(string message) => Native.Log(0, message);
        public static void LogWarning(string message) => Native.Log(1, message);
        public static void LogError(string message) => Native.Log(2, message);
    }

    public static class Time
    {
        /// <summary>Seconds since the application started.</summary>
        public static unsafe double TimeSinceStart => Native.Api.GetTimeSeconds();
    }

    /// <summary>
    /// Query engine input channels. Channel names follow O3DE conventions, e.g.
    /// "keyboard_key_alphanumeric_W", "mouse_button_left", "mouse_delta_x",
    /// "gamepad_button_a". Key helpers cover the common keyboard/mouse cases.
    /// </summary>
    public static class Input
    {
        /// <summary>True while the named input channel is active (e.g. key held).</summary>
        public static bool IsHeld(string channelName) => Native.IsChannelActive(channelName);

        /// <summary>Current analog value of the named input channel.</summary>
        public static float GetValue(string channelName) => Native.GetChannelValue(channelName);

        /// <summary>True while a keyboard key is held. Accepts "W", "Space", "LShift", "Up", "1"...</summary>
        public static bool GetKey(string key) => IsHeld(KeyChannel(key));

        public static bool GetMouseButton(int button) => IsHeld(button switch
        {
            1 => "mouse_button_right",
            2 => "mouse_button_middle",
            _ => "mouse_button_left",
        });

        public static Vector3 MouseDelta => new(GetValue("mouse_delta_x"), GetValue("mouse_delta_y"), 0.0f);

        /// <summary>System cursor position normalized to [0,1] across the window.</summary>
        public static unsafe Vector3 CursorPosition
        {
            get
            {
                float* xy = stackalloc float[2];
                Native.Api.GetCursorPositionNormalized(xy);
                return new Vector3(xy[0], xy[1], 0.0f);
            }
        }

        private static string KeyChannel(string key)
        {
            if (key.Length == 1 && (char.IsLetter(key[0]) || char.IsDigit(key[0])))
            {
                return "keyboard_key_alphanumeric_" + char.ToUpperInvariant(key[0]);
            }
            return key switch
            {
                "Space" => "keyboard_key_edit_space",
                "Enter" => "keyboard_key_edit_enter",
                "Tab" => "keyboard_key_edit_tab",
                "Backspace" => "keyboard_key_edit_backspace",
                "Escape" => "keyboard_key_escape",
                "LShift" => "keyboard_key_modifier_shift_l",
                "RShift" => "keyboard_key_modifier_shift_r",
                "LCtrl" => "keyboard_key_modifier_ctrl_l",
                "RCtrl" => "keyboard_key_modifier_ctrl_r",
                "LAlt" => "keyboard_key_modifier_alt_l",
                "RAlt" => "keyboard_key_modifier_alt_r",
                "Up" => "keyboard_key_navigation_arrow_up",
                "Down" => "keyboard_key_navigation_arrow_down",
                "Left" => "keyboard_key_navigation_arrow_left",
                "Right" => "keyboard_key_navigation_arrow_right",
                _ => key,
            };
        }
    }

    public struct RaycastHit
    {
        public Vector3 Position;
        public Vector3 Normal;
        public float Distance;
        public Entity Entity;
    }

    public static class Physics
    {
        /// <summary>Casts a ray in the default physics scene. Returns true on hit.</summary>
        public static unsafe bool Raycast(Vector3 origin, Vector3 direction, float maxDistance, out RaycastHit hit)
        {
            float* posNormal = stackalloc float[6];
            float distance = 0.0f;
            ulong entityId = 0;
            int result = Native.Api.RayCast(
                origin.X, origin.Y, origin.Z,
                direction.X, direction.Y, direction.Z,
                maxDistance, posNormal, &distance, &entityId);
            hit = new RaycastHit
            {
                Position = new Vector3(posNormal[0], posNormal[1], posNormal[2]),
                Normal = new Vector3(posNormal[3], posNormal[4], posNormal[5]),
                Distance = distance,
                Entity = new Entity(entityId),
            };
            return result != 0;
        }
    }

    /// <summary>A handle to an engine entity with transform, physics, and lifecycle access.</summary>
    public readonly struct Entity
    {
        public readonly ulong Id;

        public Entity(ulong id)
        {
            Id = id;
        }

        public bool IsValid => Id != 0;

        public static Entity Find(string name) => new(Native.FindEntityByName(name));

        /// <summary>Creates and activates a new empty entity with a transform.</summary>
        public static Entity Create(string name) => new(Native.CreateEntity(name));

        public unsafe void Destroy() => Native.Api.DestroyEntity(Id);

        public unsafe bool IsActive => Native.Api.IsEntityActive(Id) != 0;

        public unsafe void SetActive(bool active) => Native.Api.SetEntityActive(Id, active ? 1 : 0);

        public string Name => Native.GetEntityName(Id);

        public unsafe Entity Parent
        {
            get => new(Native.Api.GetParent(Id));
            set => Native.Api.SetParent(Id, value.Id);
        }

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

        public unsafe Vector3 LocalPosition
        {
            get
            {
                float* xyz = stackalloc float[3];
                Native.Api.GetLocalPosition(Id, xyz);
                return new Vector3(xyz[0], xyz[1], xyz[2]);
            }
            set => Native.Api.SetLocalPosition(Id, value.X, value.Y, value.Z);
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

        public unsafe Quaternion Rotation
        {
            get
            {
                float* xyzw = stackalloc float[4];
                Native.Api.GetWorldRotationQuaternion(Id, xyzw);
                return new Quaternion(xyzw[0], xyzw[1], xyzw[2], xyzw[3]);
            }
            set => Native.Api.SetWorldRotationQuaternion(Id, value.X, value.Y, value.Z, value.W);
        }

        public unsafe Vector3 RightVector => Basis(0);
        public unsafe Vector3 ForwardVector => Basis(1);
        public unsafe Vector3 UpVector => Basis(2);

        private unsafe Vector3 Basis(int index)
        {
            float* basis = stackalloc float[9];
            Native.Api.GetWorldBasis(Id, basis);
            return new Vector3(basis[index * 3], basis[index * 3 + 1], basis[index * 3 + 2]);
        }

        public unsafe float UniformScale
        {
            get => Native.Api.GetUniformScale(Id);
            set => Native.Api.SetUniformScale(Id, value);
        }

        // Rigid body physics (requires a Rigid Body component on the entity)

        public unsafe Vector3 LinearVelocity
        {
            get
            {
                float* xyz = stackalloc float[3];
                Native.Api.GetLinearVelocity(Id, xyz);
                return new Vector3(xyz[0], xyz[1], xyz[2]);
            }
            set => Native.Api.SetLinearVelocity(Id, value.X, value.Y, value.Z);
        }

        public unsafe Vector3 AngularVelocity
        {
            get
            {
                float* xyz = stackalloc float[3];
                Native.Api.GetAngularVelocity(Id, xyz);
                return new Vector3(xyz[0], xyz[1], xyz[2]);
            }
            set => Native.Api.SetAngularVelocity(Id, value.X, value.Y, value.Z);
        }

        public unsafe void ApplyImpulse(Vector3 impulse) => Native.Api.ApplyLinearImpulse(Id, impulse.X, impulse.Y, impulse.Z);

        public unsafe void ApplyAngularImpulse(Vector3 impulse) =>
            Native.Api.ApplyAngularImpulse(Id, impulse.X, impulse.Y, impulse.Z);

        public unsafe float Mass => Native.Api.GetMass(Id);

        public unsafe void SetGravityEnabled(bool enabled) => Native.Api.SetGravityEnabled(Id, enabled ? 1 : 0);

        public unsafe void SetKinematic(bool kinematic) => Native.Api.SetKinematic(Id, kinematic ? 1 : 0);
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
