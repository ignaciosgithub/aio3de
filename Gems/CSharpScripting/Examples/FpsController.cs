/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

// Sample C# script - copy into <project>/Scripts/FpsController.cs.
// WASD movement, mouse-look (yaw locked to the world up axis, pitch clamped,
// no roll) and Space to jump. Attach a "C# Script" component to an entity
// (ideally one with a camera child) and set Class name to "FpsController".

using AIO3DE;

public class FpsController : ScriptComponent
{
    private float _speed = 5.0f;
    private float _lookSensitivity = 0.1f;
    private float _jumpSpeed = 5.0f;
    private float _gravity = 9.81f;
    private float _groundCheckDistance = 1.1f;
    private float _yaw;
    private float _pitch;
    private float _verticalVelocity;
    private bool _jumpHeld;

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
        // Yaw around the world up axis first, then pitch around the resulting
        // local right axis. Roll is never introduced, so the character cannot
        // tilt sideways.
        Quaternion yawRotation = Quaternion.FromAxisAngle(Vector3.Up, _yaw);
        Quaternion pitchRotation = Quaternion.FromAxisAngle(Vector3.Right, _pitch);
        Entity.Rotation = (yawRotation * pitchRotation).Normalized();

        // Move on the ground plane using yaw only, so looking up or down
        // doesn't change the movement speed.
        Vector3 forward = yawRotation.Rotate(Vector3.Forward);
        Vector3 right = yawRotation.Rotate(Vector3.Right);
        Vector3 move = Vector3.Zero;
        if (Input.GetKey("W"))
        {
            move += forward;
        }
        if (Input.GetKey("S"))
        {
            move -= forward;
        }
        if (Input.GetKey("D"))
        {
            move += right;
        }
        if (Input.GetKey("A"))
        {
            move -= right;
        }
        move.Z = 0.0f;
        if (move.LengthSquared() > 0.0f)
        {
            float speed = Input.GetKey("LShift") ? _speed * 2.0f : _speed;
            Entity.Position += move.Normalized() * (speed * deltaTime);
        }

        bool grounded = Physics.Raycast(
            Entity.Position, -Vector3.Up, _groundCheckDistance, out RaycastHit _);

        bool jumpPressed = Input.GetKey("Space");
        if (grounded && _verticalVelocity <= 0.0f)
        {
            _verticalVelocity = 0.0f;
            if (jumpPressed && !_jumpHeld)
            {
                _verticalVelocity = _jumpSpeed;
            }
        }
        else
        {
            _verticalVelocity -= _gravity * deltaTime;
        }
        _jumpHeld = jumpPressed;

        if (_verticalVelocity != 0.0f)
        {
            Entity.Position += Vector3.Up * (_verticalVelocity * deltaTime);
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
