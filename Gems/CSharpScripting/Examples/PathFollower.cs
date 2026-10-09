using AIO3DE;

/// <summary>
/// Moves this entity along a 3D voxel path (VoxelNav gem) to the Target entity, re-planning periodically.
/// Setup: add a C# Script component with class name "PathFollower", pick the Target in the Inspector and make
/// sure a Voxel Nav Volume covers both entities. Works for Walk volumes (points are feet positions) and
/// Fly volumes (points are voxel centres) alike.
/// </summary>
public class PathFollower : ScriptComponent
{
    public Entity Target;
    public float Speed = 3.0f;
    public float RepathInterval = 0.5f;
    public float ArriveDistance = 0.25f;
    public bool LogPath = false;

    private Vector3[] _path = System.Array.Empty<Vector3>();
    private int _waypoint;
    private float _repathTimer;

    public override void OnActivate()
    {
        _repathTimer = 0.0f;
    }

    public override void OnUpdate(float deltaTime)
    {
        _repathTimer -= deltaTime;
        if (_repathTimer <= 0.0f)
        {
            _repathTimer = RepathInterval;
            Repath();
        }

        if (_waypoint >= _path.Length)
        {
            return;
        }

        Vector3 position = Entity.Position;
        Vector3 toTarget = _path[_waypoint] - position;
        float distance = toTarget.Length();
        if (distance <= ArriveDistance)
        {
            _waypoint++;
            return;
        }

        float step = System.Math.Min(distance, Speed * deltaTime);
        Entity.Position = position + toTarget * (step / distance);

        Vector3 flat = new Vector3(toTarget.X, toTarget.Y, 0.0f);
        if (flat.Length() > 0.001f)
        {
            float yawDegrees = (float)(System.Math.Atan2(flat.X, flat.Y) * 180.0 / System.Math.PI);
            Entity.Rotation = Quaternion.FromAxisAngle(Vector3.Up, -yawDegrees);
        }
    }

    private void Repath()
    {
        if (!Target.IsValid || !Pathfinding.IsReady)
        {
            return;
        }

        Vector3 start = Entity.Position;
        Vector3 goal = Target.Position;
        _path = Pathfinding.FindPath(start, goal);
        _waypoint = _path.Length > 1 ? 1 : _path.Length; // index 0 is our own position

        if (LogPath)
        {
            Debug.Log(_path.Length == 0
                ? $"PathFollower: no path from {start} to {goal}"
                : $"PathFollower: {_path.Length} waypoints to {goal}");
        }
    }
}
