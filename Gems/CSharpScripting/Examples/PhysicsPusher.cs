/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

// Sample C# script - copy into <project>/Scripts/PhysicsPusher.cs.
// RequireComponent-gated physics playground: only runs when the entity has a
// PhysX Rigid Body component. Demonstrates impulses, velocity, and changing
// rigid-body parameters (gravity, kinematic) at runtime.
//
// Keys: F = impulse up, T = torque spin, G = toggle gravity, K = toggle kinematic,
//       V = print velocity/mass, X = stop dead.

using AIO3DE;

[RequireComponent("RigidBody")]
public class PhysicsPusher : ScriptComponent
{
    public float ImpulseStrength = 5.0f;
    public float TorqueStrength = 2.0f;

    private bool _gravityEnabled = true;
    private bool _kinematic;
    private bool _fWasDown, _tWasDown, _gWasDown, _kWasDown, _vWasDown, _xWasDown;

    public override void OnActivate()
    {
        // Never reached unless the entity has a Rigid Body component.
        Debug.Log($"PhysicsPusher active on '{Entity.Name}' (mass {Entity.Mass:F2})");
    }

    public override void OnUpdate(float deltaTime)
    {
        if (Pressed("F", ref _fWasDown))
        {
            Entity.ApplyImpulse(new Vector3(0.0f, 0.0f, ImpulseStrength));
        }
        if (Pressed("T", ref _tWasDown))
        {
            Entity.ApplyAngularImpulse(new Vector3(0.0f, 0.0f, TorqueStrength));
        }
        if (Pressed("G", ref _gWasDown))
        {
            _gravityEnabled = !_gravityEnabled;
            Entity.SetGravityEnabled(_gravityEnabled);
            Debug.Log($"Gravity {(_gravityEnabled ? "on" : "off")}");
        }
        if (Pressed("K", ref _kWasDown))
        {
            _kinematic = !_kinematic;
            Entity.SetKinematic(_kinematic);
            Debug.Log($"Kinematic {(_kinematic ? "on" : "off")}");
        }
        if (Pressed("V", ref _vWasDown))
        {
            Vector3 v = Entity.LinearVelocity;
            Debug.Log($"Velocity {v.X:F2},{v.Y:F2},{v.Z:F2}  mass {Entity.Mass:F2}");
        }
        if (Pressed("X", ref _xWasDown))
        {
            Entity.LinearVelocity = Vector3.Zero;
            Entity.AngularVelocity = Vector3.Zero;
        }
    }

    private static bool Pressed(string key, ref bool wasDown)
    {
        bool down = Input.GetKey(key);
        bool pressed = down && !wasDown;
        wasDown = down;
        return pressed;
    }
}
