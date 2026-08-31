/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

// Sample C# script - copy into <project>/Scripts/FpsController.cs.
// Keyboard/mouse: WASD move, mouse-look, Space jump, left click shoot.
// Gamepad: left stick move, right stick look, A jump, R2 (or R1) shoot.
// Mouse-look yaw is locked to the world up axis with clamped pitch (no roll).
// Attach a "C# Script" component to an entity (ideally one with a camera
// child) and set Class name to "FpsController".

using AIO3DE;

public class FpsController : ScriptComponent
{
    private float _speed = 5.0f;
    private float _lookSensitivity = 0.1f;
    private float _gamepadLookSpeed = 120.0f; // degrees per second at full stick
    private float _jumpSpeed = 5.0f;
    private float _gravity = 9.81f;
    private float _groundCheckDistance = 1.1f;
    // Artificial drag: fraction of residual (external) rigid-body velocity
    // removed per second, so momentary impacts decay instead of pushing the
    // character forever. Intended movement is not dragged. Set to 0 to disable.
    private float _linearDrag = 8.0f;
    private float _angularDrag = 12.0f;
    private float _yaw;
    private float _pitch;
    private float _verticalVelocity;
    private bool _jumpHeld;
    private bool _hasRigidBody;
    private Vector3 _lastDesiredVelocity;

    public override void OnActivate()
    {
        Vector3 euler = Entity.RotationEuler;
        _pitch = euler.X;
        _yaw = euler.Z;
        _hasRigidBody = Entity.Mass > 0.0f;
        Debug.Log($"FpsController active on '{Entity.Name}'");
    }

    public override void OnUpdate(float deltaTime)
    {
        // Look: mouse delta + gamepad right stick.
        Vector3 look = Input.MouseDelta;
        _yaw -= look.X * _lookSensitivity;
        _pitch -= look.Y * _lookSensitivity;
        float stickLookX = Input.GetValue("gamepad_thumbstick_r_x");
        float stickLookY = Input.GetValue("gamepad_thumbstick_r_y");
        _yaw -= stickLookX * _gamepadLookSpeed * deltaTime;
        _pitch += stickLookY * _gamepadLookSpeed * deltaTime;
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
        // doesn't change the movement speed. Keyboard + gamepad left stick.
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
        float stickMoveX = Input.GetValue("gamepad_thumbstick_l_x");
        float stickMoveY = Input.GetValue("gamepad_thumbstick_l_y");
        move += right * stickMoveX + forward * stickMoveY;
        move.Z = 0.0f;
        if (move.LengthSquared() > 1.0f)
        {
            move = move.Normalized();
        }
        float speed = Input.GetKey("LShift") || Input.IsHeld("gamepad_button_l3")
            ? _speed * 2.0f
            : _speed;
        Vector3 desiredVelocity = move * speed;

        bool grounded = Physics.Raycast(
            Entity.Position, -Vector3.Up, _groundCheckDistance, out RaycastHit _);
        bool jumpPressed = Input.GetKey("Space") || Input.IsHeld("gamepad_button_a");

        if (_hasRigidBody)
        {
            // Velocity-driven movement: intended velocity is applied at full
            // strength every frame; only the residual (external) velocity from
            // collisions/impulses is dragged, so walking never feels slow.
            Vector3 velocity = Entity.LinearVelocity;
            Vector3 residual = velocity - _lastDesiredVelocity;
            residual.Z = 0.0f;
            if (_linearDrag > 0.0f)
            {
                residual *= 1.0f / (1.0f + _linearDrag * deltaTime);
            }
            float verticalVelocity = velocity.Z;
            if (grounded && jumpPressed && !_jumpHeld)
            {
                verticalVelocity = _jumpSpeed;
            }
            Vector3 newVelocity = desiredVelocity + residual;
            newVelocity.Z = verticalVelocity;
            Entity.LinearVelocity = newVelocity;
            _lastDesiredVelocity = desiredVelocity;

            if (_angularDrag > 0.0f)
            {
                Entity.AngularVelocity *= 1.0f / (1.0f + _angularDrag * deltaTime);
            }
        }
        else
        {
            // No rigid body: move the transform directly with manual gravity.
            if (desiredVelocity.LengthSquared() > 0.0f)
            {
                Entity.Position += desiredVelocity * deltaTime;
            }
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
            if (_verticalVelocity != 0.0f)
            {
                Entity.Position += Vector3.Up * (_verticalVelocity * deltaTime);
            }
        }
        _jumpHeld = jumpPressed;

        bool shoot = Input.GetMouseButton(0)
            || Input.GetValue("gamepad_trigger_r2") > 0.5f
            || Input.IsHeld("gamepad_button_r1");
        if (shoot)
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
