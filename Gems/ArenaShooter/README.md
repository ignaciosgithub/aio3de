# Arena Shooter Kit

Example game kit for building an arena shooter: ready-to-use input bindings
(keyboard/mouse **and** gamepad, both active at once), player controller,
hitscan weapon, health/respawn, score + match timer HUD, input device
detection, and an animation driver for rigged characters.

Everything ships as example project files (Lua scripts + an `.inputbindings`
asset) — enable the gem and drop them onto entities.

## Enable

```
scripts\o3de.bat enable-gem -gn ArenaShooter -pp <your project path>
```

No rebuild is required (asset-only gem) — restart the Editor/Asset Processor
and the assets appear under `Gems/ArenaShooter/Assets` in the Asset Browser.

Required gems in your project: **PhysX5** (character/rigid bodies),
**StartingPointInput** (input), **DebugDraw** (HUD text and impact markers).

## Build the arena level

1. **Ground/arena**: create a level; use Terrain or a large box with a PhysX
   Static Rigid Body + Collider. Block out walls/ramps/cover with **White Box**
   components or boxes.
2. **Player** entity:
   - PhysX **Character Controller** + **Character Gameplay** components
   - **Input** component → assign `arenashooter.inputbindings`
   - **Lua Script** → `PlayerController.lua` (set *CameraEntity* to the child below)
   - **Lua Script** → `Weapon.lua` (set *CameraEntity* the same). For
     multiple guns add one `Weapon.lua` per gun with unique *Slot* values
     (1..N) and different Damage/FireInterval/Range, plus one
     **Lua Script** → `WeaponSwitcher.lua` (*WeaponCount* = N): switch with
     the mouse **scroll wheel**, **Q/E**, or gamepad **LB/RB**.
   - **Lua Script** → `Health.lua`
   - child entity with a **Camera** component, placed at eye height
3. **Targets** (things to shoot): any entity with a PhysX Collider
   (+ Rigid Body if it should be knocked around) and a **Lua Script** →
   `Health.lua`. Set *SpawnPoint* to an empty entity to control respawns.
4. **Game manager** entity:
   - **Lua Script** → `ScoreHud.lua`: add every target entity to *Targets*;
     set *MatchTime*.
   - **Lua Script** → `DeviceDetector.lua`: broadcasts `ActiveInputDevice`
     (1 = keyboard/mouse, 2 = gamepad) whenever the player switches device.
   - **Input** component → `arenashooter.inputbindings` (needed so the
     device-activity events reach the detector).
5. Press **Ctrl+G**. WASD/left stick to move, mouse/right stick to aim,
   LMB/right trigger to shoot, Space/A to jump.

## Animated characters

Give your rigged character (Actor + Anim Graph components) a **Lua Script** →
`AnimationDriver.lua`. It feeds two float parameters to the anim graph:

- `MoveSpeed` — planar speed in m/s (drive idle/walk/run blends with it)
- `Shooting` — 1 while firing (drive a shoot/recoil state with it)

Create those parameters in the Animation Editor and use them in your state
machine transition conditions. Set *SourceEntity* to the player root entity if
the actor is a child.

For a **death animation**, add **Lua Script** → `DeathFx.lua` to the same
entity: it drives a `Dead` float parameter (1 while dead, 0 after respawn)
off the `Killed`/`Respawned` events, so transition into your death state on
`Dead > 0.5` and back out on respawn. It can also play a death sound.

## Sound (stereo + 3D positional)

Enable the **MiniAudio** gem for audio: put a **MiniAudio Listener**
component on the camera (ears follow the view) and a **MiniAudio Playback**
component on each sound-emitting entity — check *Enable Spatialization* for
3D positional audio (distance attenuation + stereo panning), leave it off
for flat stereo (music/UI). Each playback component has its own volume.
`Weapon.lua` (*FireSoundEntity*), `WeaponSwitcher.lua` (*SwitchSoundEntity*)
and `DeathFx.lua` (*DeathSoundEntity*) trigger these playback entities
automatically when assigned.

## Events (for extending the game)

All cross-script communication uses gameplay notification events (float
payload), so Script Canvas or other Lua scripts can hook in:

| Event | Channel entity | Meaning |
| --- | --- | --- |
| `Damage` | damaged entity | apply damage (value = amount) |
| `Killed` | dying entity | entity just died |
| `Respawned` | respawned entity | entity came back |
| `WeaponSlot` | player entity | active weapon slot changed (value = slot) |
| `MoveSpeed` | player entity | planar speed every tick |
| `ActiveInputDevice` | detector entity | 1 = KB/M, 2 = gamepad |

Input events (bindable in the Input component or listenable from scripts):
`MoveForward`, `MoveRight`, `LookX`, `LookY`, `Shoot`, `Jump`,
`WeaponScroll` (mouse wheel), `WeaponNext`/`WeaponPrev` (E/Q, RB/LB),
`Vote1`–`Vote4` (keys 1–4, d-pad — used by the multiplayer map vote), plus
`KMActivity`/`PadActivity` used by the device detector.
