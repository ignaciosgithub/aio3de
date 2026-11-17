# Platformer Project

Press **Ctrl+G** in the Editor to play. Use **WASD** to move, the mouse to look,
and **Space** to jump and double-jump. Reach the goal by crossing moving
platforms, avoiding behavior-tree enemies, and collecting coins.

The course demonstrates World Partition streaming, VoxelNav, FSR 1 upscaling,
ray-traced shadows, portable GI, and hot-reloaded C++ game code. Gameplay
scripts are in `Scripts/Platformer`; the streamed course is under `Levels/Course`.

Regenerate the course from the Editor with:

```text
Editor --project-path <project> --runpython Editor/Scripts/build_course_level.py
```
