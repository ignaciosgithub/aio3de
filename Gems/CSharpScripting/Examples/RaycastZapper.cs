/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

// Sample C# script - copy into <project>/Scripts/RaycastZapper.cs.
// Physics raycasts + manual component checks on other entities: press E to cast
// a ray forward; if it hits an entity with a Rigid Body the hit is pushed away,
// otherwise the hit is only reported. Shows changing another entity's physics
// parameters (velocity/impulse) rather than this entity's.

using AIO3DE;

public class RaycastZapper : ScriptComponent
{
    public float Range = 25.0f;
    public float PushStrength = 8.0f;

    private bool _eWasDown;

    public override void OnUpdate(float deltaTime)
    {
        bool eDown = Input.GetKey("E");
        if (eDown && !_eWasDown)
        {
            Zap();
        }
        _eWasDown = eDown;
    }

    private void Zap()
    {
        Vector3 origin = Entity.Position + Entity.ForwardVector * 0.5f;
        if (!Physics.Raycast(origin, Entity.ForwardVector, Range, out RaycastHit hit))
        {
            Debug.Log("Zap: nothing in range");
            return;
        }

        Debug.Log($"Zap hit '{hit.Entity.Name}' at {hit.Distance:F1}m");

        // Component-gated behavior on OTHER entities: only push things that
        // actually have a rigid body, so static geometry is left alone.
        if (hit.Entity.HasComponent("RigidBody"))
        {
            hit.Entity.ApplyImpulse(Entity.ForwardVector * PushStrength);
        }
    }
}
