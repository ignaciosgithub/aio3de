/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

// Sample C# script - copy into <project>/Scripts/ComponentTweaker.cs.
// Generic component access: list components and their properties, read/write any
// reflected property by name, add/remove components at runtime, and call into another
// script on a different entity (Entity.GetScript<T>()).
//
// Keys: L = list components + rigid-body properties, D = double the rigid body's linear damping,
//       B = add a Box Shape collider (if missing), N = remove the Tag component,
//       C = call a method on the Target entity's PhysicsPusher script.

using AIO3DE;

public class ComponentTweaker : ScriptComponent
{
    /// <summary>Entity whose PhysicsPusher script we talk to (pick it in the Inspector).</summary>
    public Entity Target;

    private bool _lWasDown, _dWasDown, _bWasDown, _nWasDown, _cWasDown;

    public override void OnActivate()
    {
        Debug.Log($"'{Entity.Name}' components: {string.Join(", ", Entity.Components)}");

        // Missing components are reported, never thrown.
        Component light = Entity.GetComponent("PointLight");
        if (!light.IsValid)
        {
            Debug.Log("No light on this entity - GetComponent returned an invalid handle.");
        }
    }

    public override void OnUpdate(float deltaTime)
    {
        if (Pressed("L", ref _lWasDown))
        {
            Component body = Entity.GetComponent("RigidBody");
            if (!body.IsValid)
            {
                Debug.Log("No Rigid Body on this entity.");
                return;
            }
            foreach (string record in body.Properties)
            {
                // "RigidBodyConfiguration/Linear damping|float"
                Debug.Log("  " + record);
            }
            Debug.Log($"Mass={body.GetFloat("Mass")}  Gravity={body.GetBool("Gravity Enabled")}  " +
                      $"Damping={body.GetFloat("Linear damping")}  Kinematic={body.GetBool("Kinematic")}");
        }

        if (Pressed("D", ref _dWasDown))
        {
            Component body = Entity.GetComponent("RigidBody");
            float damping = body.GetFloat("LinearDamping"); // spaces/case are ignored when matching names
            // Rigid bodies read their configuration on activation, so ask for a reactivate.
            if (body.Set("Linear damping", damping * 2.0f + 0.1f, reactivate: true))
            {
                Debug.Log($"Linear damping {damping} -> {damping * 2.0f + 0.1f} (applies next frame)");
            }
            else
            {
                Debug.Log("Could not set 'Linear damping' - is there a Rigid Body?");
            }
        }

        if (Pressed("B", ref _bWasDown))
        {
            if (Entity.HasComponent("BoxShape"))
            {
                Component box = Entity.GetComponent("BoxShape");
                box.Set("Dimensions", new Vector3(2.0f, 2.0f, 2.0f), reactivate: true);
                Debug.Log("Box Shape already present - grew it to 2x2x2.");
            }
            else if (Entity.AddComponent("BoxShape"))
            {
                Debug.Log("Box Shape added (entity reactivates next frame).");
            }
        }

        if (Pressed("N", ref _nWasDown))
        {
            Debug.Log(Entity.RemoveComponent("Tag") ? "Tag component removed." : "No Tag component to remove.");
        }

        if (Pressed("C", ref _cWasDown))
        {
            // Script-to-script call: another entity's C# script as a typed object.
            PhysicsPusher? pusher = Target.GetScript<PhysicsPusher>();
            if (pusher == null)
            {
                Debug.Log("Target has no active PhysicsPusher script.");
                return;
            }
            pusher.ImpulseStrength *= 1.5f;
            Debug.Log($"Target impulse strength is now {pusher.ImpulseStrength}");
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
