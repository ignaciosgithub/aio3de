// Sample C# script — copy into <project>/Scripts/Mover.cs.
// Attach a "C# Script" component to an entity and set Class name to "Mover".

using AIO3DE;

public class Mover : ScriptComponent
{
    private float _time;

    public override void OnActivate()
    {
        Debug.Log($"Mover activated on entity '{Entity.Name}' at {Entity.Position}");
    }

    public override void OnUpdate(float deltaTime)
    {
        _time += deltaTime;
        // Bob up and down and spin slowly.
        Vector3 position = Entity.Position;
        position.Z += System.MathF.Sin(_time * 2.0f) * deltaTime;
        Entity.Position = position;

        Vector3 rotation = Entity.RotationEuler;
        rotation.Z += 45.0f * deltaTime;
        Entity.RotationEuler = rotation;
    }

    public override void OnDeactivate()
    {
        Debug.Log("Mover deactivated");
    }
}
