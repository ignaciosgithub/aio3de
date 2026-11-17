# FPS Project

Press **Ctrl+G** in the Editor to play. Use **WASD** to move, the mouse to aim,
**left mouse** to shoot, **Space** to jump, **Q/E** or the mouse wheel to switch
weapons, and **F10** to open settings.

The arena demonstrates BehaviorTree patrol/chase/shoot enemies with VoxelNav
pathfinding, FSR 1 upscaling, ray-traced shadows, portable GI, and hot-reloaded
C++ game code. The AI lives in `Scripts/Fps`; the arena and its navigation
volume are under `Levels/Arena`.

On a weaker GPU, enter these console commands:

```text
r_upscaler 0
r_rayTracedShadows false
r_portableGi false
```

To demonstrate hot reload, change the default `DegreesPerSecond` value in
`Gem/Source/${Name}SpinnerComponent.cpp`, rebuild the `${Name}` game gem while
the Editor remains open, and watch the floating pickups change speed.

Regenerate the arena from the Editor with:

```text
Editor --project-path <project> --runpython Editor/Scripts/build_arena_level.py
```
