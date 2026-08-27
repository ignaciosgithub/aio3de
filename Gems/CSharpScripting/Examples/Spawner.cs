/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

// Sample C# script - copy into <project>/Scripts/Spawner.cs.
// Spawns a prefab when Space is pressed, tags it, despawns the oldest when
// more than MaxAlive exist, and reports collisions with this entity.

using System.Collections.Generic;
using AIO3DE;

public class Spawner : ScriptComponent
{
    // Cache-relative path of the processed prefab (AssetProcessor turns .prefab into .spawnable)
    private const string SpawnablePath = "prefabs/enemy.spawnable";
    private const int MaxAlive = 5;

    private readonly Queue<PrefabInstance> _alive = new();
    private bool _spaceWasDown;

    public override void OnUpdate(float deltaTime)
    {
        bool spaceDown = Input.GetKey("Space");
        if (spaceDown && !_spaceWasDown)
        {
            PrefabInstance instance = Prefab.Spawn(SpawnablePath, Entity.Position + Entity.ForwardVector * 2.0f);
            if (instance.IsValid)
            {
                _alive.Enqueue(instance);
                if (_alive.Count > MaxAlive)
                {
                    _alive.Dequeue().Despawn();
                }
            }
        }
        _spaceWasDown = spaceDown;

        // RootEntity becomes valid once the async spawn completes (usually next frame)
        foreach (PrefabInstance instance in _alive)
        {
            Entity root = instance.RootEntity;
            if (root.IsValid && !root.HasTag("spawned"))
            {
                root.AddTag("spawned");
            }
        }
    }

    public override void OnCollisionEnter(Collision collision)
    {
        string label = collision.Other.HasTag("spawned") ? "a spawned prefab" : collision.Other.Name;
        Debug.Log($"Hit by {label} at {collision.Position.X:F1},{collision.Position.Y:F1},{collision.Position.Z:F1} (impulse {collision.Impulse:F1})");
    }

    public override void OnCollisionExit(Entity other)
    {
        Debug.Log($"No longer touching entity {other.Id}");
    }

    public override void OnDeactivate()
    {
        while (_alive.Count > 0)
        {
            _alive.Dequeue().Despawn();
        }
    }
}
