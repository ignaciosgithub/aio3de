# GameSettings gem

Persistent per-user game settings for any project: named float values (FOV,
sensitivity, volume, ...) and rebindable input, saved to
`<project user folder>/gamesettings.json` and loaded on startup.

## Enable

```
scripts\o3de.bat enable-gem -gn GameSettings -pp <your project path>
```

(Also enable **StartingPointInput** — the Remappable Input component emits
its input events.)

## Float settings (FOV, sensitivity, ...)

From Lua / Script Canvas:

```lua
local fov = GameSettingsRequestBus.Broadcast.GetValue("fov", 75.0)
GameSettingsRequestBus.Broadcast.SetValue("fov", 90.0)   -- raises OnSettingChanged
GameSettingsRequestBus.Broadcast.Save()
```

Names are free-form — the ArenaShooter settings menu uses `fov`,
`mouse_sensitivity` and `pad_sensitivity`.

## Rebindable input

The **Remappable Input** component replaces (or complements) the Input
component: each row maps a raw input channel (e.g. `mouse_button_left`) to a
StartingPointInput event (e.g. `Shoot`), so existing scripts and networked
components keep working unchanged. Per row:

- **Binding Key** — the settings key the user's rebind is stored under;
  empty rows are fixed.
- **Default Channel** — used until the user rebinds.
- **Scale Setting / Scale Default** — optional named float multiplying the
  event value; point look axes at `mouse_sensitivity` / `pad_sensitivity`
  to get live sensitivity, including for the networked arena player.

Rebinding at runtime:

```lua
GameSettingsRequestBus.Broadcast.StartRebind("bind_shoot") -- next key/button pressed becomes the binding
```

The capture accepts keyboard keys, mouse buttons, and gamepad
buttons/triggers (movement axes are ignored so a mouse twitch can't bind),
consumes the captured press, saves the override, and raises
`OnBindingChanged` — active Remappable Input components re-resolve
immediately, so changes apply live (e.g. during warm-up or mid-match
downtime).

## ArenaShooter settings menu

The ArenaShooter kit ships `SettingsMenu.lua` (put it on the game manager
entity, set *CameraEntity*): **F10** / gamepad **start** toggles it,
arrows / d-pad navigate, left/right adjusts FOV and mouse/gamepad
sensitivity, **Enter**/**A** starts a key rebind. All changes apply live
and persist.
