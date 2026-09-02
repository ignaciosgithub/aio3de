/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

// Sample C# script - copy into <project>/Scripts/FollowTarget.cs.
// Other entities as Inspector variables: drag any entity from the Outliner
// onto the Target field, or fall back to a name lookup. Follows the target
// at FollowSpeed while keeping StopDistance.

using AIO3DE;

public class FollowTarget : ScriptComponent
{
    // Entity fields show an entity picker in the Inspector (drag & drop from the Outliner).
    public Entity Target;

    // Fallback: resolved by name at activation when no Target was assigned.
    public string TargetName = "";

    public float FollowSpeed = 3.0f;
    public float StopDistance = 2.0f;

    public override void OnActivate()
    {
        if (!Target.IsValid && TargetName.Length > 0)
        {
            Target = Entity.Find(TargetName);
        }
        if (!Target.IsValid)
        {
            Debug.LogWarning($"FollowTarget on '{Entity.Name}': no target assigned.");
        }
    }

    public override void OnUpdate(float deltaTime)
    {
        if (!Target.IsValid)
        {
            return;
        }

        Vector3 toTarget = Target.Position - Entity.Position;
        float distance = toTarget.Length();
        if (distance > StopDistance && distance > 0.001f)
        {
            Entity.Position += toTarget * (1.0f / distance) * FollowSpeed * deltaTime;
        }
    }
}
