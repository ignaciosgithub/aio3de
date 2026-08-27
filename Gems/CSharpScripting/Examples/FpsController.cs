/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

// Sample C# script - copy into <project>/Scripts/FpsController.cs.
// WASD movement with mouse-look. Attach a "C# Script" component to an entity
// (ideally one with a camera child) and set Class name to "FpsController".

using AIO3DE;

public class FpsController : ScriptComponent
{
    private float _speed = 5.0f;
    private float _lookSensitivity = 0.1f;
    private float _yaw;
    private float _pitch;

    public override void OnActivate()
    {
        Vector3 euler = Entity.RotationEuler;
        _pitch = euler.X;
        _yaw = euler.Z;
        Debug.Log($"FpsController active on '{Entity.Name}'");
    }

    public override void OnUpdate(float deltaTime)
    {
        Vector3 look = Input.MouseDelta;
        _yaw -= look.X * _lookSensitivity;
        _pitch -= look.Y * _lookSensitivity;
        if (_pitch > 89.0f)
        {
            _pitch = 89.0f;
        }
        if (_pitch < -89.0f)
        {
            _pitch = -89.0f;
        }
        Entity.RotationEuler = new Vector3(_pitch, 0.0f, _yaw);

        Vector3 move = Vector3.Zero;
        if (Input.GetKey("W"))
        {
            move += Entity.ForwardVector;
        }
        if (Input.GetKey("S"))
        {
            move -= Entity.ForwardVector;
        }
        if (Input.GetKey("D"))
        {
            move += Entity.RightVector;
        }
        if (Input.GetKey("A"))
        {
            move -= Entity.RightVector;
        }
        move.Z = 0.0f;
        if (move.LengthSquared() > 0.0f)
        {
            float speed = Input.GetKey("LShift") ? _speed * 2.0f : _speed;
            Entity.Position += move.Normalized() * (speed * deltaTime);
        }

        if (Input.GetMouseButton(0))
        {
            Vector3 origin = Entity.Position;
            if (Physics.Raycast(origin, Entity.ForwardVector, 100.0f, out RaycastHit hit))
            {
                Debug.Log($"Hit '{hit.Entity.Name}' at {hit.Position} (distance {hit.Distance})");
                hit.Entity.ApplyImpulse(Entity.ForwardVector * 50.0f);
            }
        }
    }
}
