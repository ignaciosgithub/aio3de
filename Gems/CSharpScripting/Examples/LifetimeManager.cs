/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

// Sample C# script - copy into <project>/Scripts/LifetimeManager.cs.
// Spawning and destroying entities (itself and others):
// N = create an empty runtime entity, B = spawn a prefab, J = despawn the last
// spawned prefab, Delete = destroy the Victim entity, and the script destroys
// its own entity after SelfDestructSeconds (0 = never).

using System.Collections.Generic;
using AIO3DE;

public class LifetimeManager : ScriptComponent
{
    // Assign in the Inspector: the entity destroyed when Delete is pressed.
    public Entity Victim;

    public string SpawnablePath = "prefabs/enemy.spawnable";
    public float SelfDestructSeconds = 0.0f;

    private readonly Stack<PrefabInstance> _spawned = new();
    private float _age;
    private int _created;
    private bool _nWasDown, _bWasDown, _jWasDown, _deleteWasDown;
    private bool _destroyed;

    public override void OnUpdate(float deltaTime)
    {
        if (_destroyed)
        {
            return; // Entity.Destroy() was called; treat this handle as dead.
        }

        if (Pressed("N", ref _nWasDown))
        {
            Entity created = Entity.Create($"Runtime_{++_created}");
            created.Position = Entity.Position + Entity.ForwardVector * 2.0f;
            Debug.Log($"Created empty entity '{created.Name}'");
        }

        if (Pressed("B", ref _bWasDown))
        {
            PrefabInstance instance = Prefab.Spawn(SpawnablePath, Entity.Position + Entity.UpVector * 3.0f);
            if (instance.IsValid)
            {
                _spawned.Push(instance);
            }
        }

        if (Pressed("J", ref _jWasDown) && _spawned.Count > 0)
        {
            _spawned.Pop().Despawn();
        }

        if (Pressed("keyboard_key_navigation_delete", ref _deleteWasDown) && Victim.IsValid)
        {
            Debug.Log($"Destroying '{Victim.Name}'");
            Victim.Destroy();
            Victim = default; // Never use an entity handle after destroying it.
        }

        _age += deltaTime;
        if (SelfDestructSeconds > 0.0f && _age >= SelfDestructSeconds)
        {
            Debug.Log($"'{Entity.Name}' self-destructing");
            _destroyed = true;
            Entity.Destroy(); // Destroying our own entity deactivates this script.
        }
    }

    public override void OnDeactivate()
    {
        while (_spawned.Count > 0)
        {
            _spawned.Pop().Despawn();
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
