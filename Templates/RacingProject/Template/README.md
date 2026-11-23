# Racing Project

Press **Ctrl+G** in the Editor to race three laps against three AI cars. Use
**W/S** or the left stick Y axis to throttle and brake/reverse, **A/D** or the
left stick X axis to steer, **Space** to reset, and **F10** to open the settings
menu.

The circuit demonstrates raycast suspension, arcade handling, behavior-tree AI,
lap timing and live positions, World Partition streaming, FSR upscaling,
ray-traced shadows, and portable GI. Car, race, AI, camera, and HUD scripts are
in `Scripts/Racing`; the streamed circuit prefab is `Levels/Circuit/Circuit.prefab`.
The C++ trophy spinner lives in the project game gem under `Gem/Source`.

Disable the rendering features from the in-game console with:

```text
r_upscaler 0
r_rayTracedShadows false
r_portableGi false
```

Regenerate the circuit from the Editor with:

```text
Editor --project-path <project> --runpython Editor/Scripts/build_circuit_level.py
```
