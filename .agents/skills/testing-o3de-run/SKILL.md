---
name: testing-o3de-run
description: How to end-to-end test the 'o3de run' / 'export-source' CLI and GameLauncher on a headless Linux box for the aio3de engine repo.
---

# Testing 'o3de run' on this box (Linux, headless)

## Environment layout
- Engine checkout: /home/ubuntu/repos/aio3de, registered in ~/.o3de/o3de_manifest.json.
- Prebuilt engine build tree: /home/ubuntu/repos/aio3de/build/linux (Ninja Multi-Config, profile). AssetProcessorBatch/AssetProcessor/Editor are at build/linux/bin/profile — this matches run_project.py's find_tool() candidate `engine_root/build/<platform>/bin/<config>`.
- Registered sample projects: /home/ubuntu/O3DEProjects/TestGame (already configured, assets cached) and /home/ubuntu/O3DEProjects/test. Prefer TestGame.
- 3rdParty packages: /home/ubuntu/.o3de/3rdParty (hub.get_third_party_path()); ~/o3de-packages also exists.

## Invoking the CLI
- Always use `./scripts/o3de.sh` (bootstraps the Python venv). `python3 scripts/o3de.py` fails with ModuleNotFoundError: resolvelib.
- Golden path: `./scripts/o3de.sh run --project /home/ubuntu/O3DEProjects/TestGame`. Incremental launcher build ~8 min on 8 cores; a full fresh configure+build of a new project tree takes far longer — avoid on time budget (abort after configure kicks off and report).
- Do NOT `cp -a` a project including build/ to /tmp for the stale-cache test — the build tree is tens of GB and fills the disk. Instead tar-copy excluding build/Cache/user and copy only build/linux/CMakeCache.txt.

## Running the GameLauncher headless
- Display :0 is a TigerVNC server (recordings capture it). No window manager: use `xterm -geometry` for sizing and drive it with xdotool (windowfocus + type/key). wmctrl does not work (no WM).
- Force null renderer: launcher accepts `-rhi=null` directly; for `o3de run` (which doesn't forward args) drop a setreg at <project>/user/Registry/rhi_null.setreg: {"O3DE":{"Atom":{"RHI":{"FactoryManager":{"factoriesPriority":["null"]}}}}}. Remove it after testing.
- Success bar for launch: log shows "AssetCatalog: Loaded registry containing N assets" and "Game Level Load Time ... LEVEL_LOAD_END". A "Startup Errors" modal with ALSA sound-card errors is a headless-box artifact (no sound card), not a regression; dismiss with xdotool key --window <id> Return.
- Disable AP auto-launch for negative tests with `-bg_ConnectToAssetProcessor=false` (cvar works on the command line).

## Editor-driven repro / crash hunting (headless)
- Editor runs headless with: `DISPLAY=:0 ./Editor --project-path=<proj> -rhi=null --skipWelcomeScreenDialog --autotest_mode --runpython <script.py>` (from build/linux/bin/profile). Startup takes ~3-4 min; the Python script markers land in <project>/user/log/Editor.log.
- Useful EditorPython recipe: `general.idle_enable(True)`, `open_level_no_prompt(...)`, create entity via `editor.ToolsApplicationRequestBus(bus.Broadcast, "CreateNewEntity", entity.EntityId())`, add "Light" via `FindComponentTypeIdsByEntityType(["Light"], 0)` + `AddComponentsOfType`, set props via `SetComponentProperty` (paths like `Controller|Configuration|Shadows|Cache shadows`), console cvars via `general.run_console("r_rayTracedShadows 1")`, finish with `general.exit_no_prompt()`.
- To capture crashes run under `gdb -batch -ex "handle SIGPIPE nostop noprint" -ex run -ex "thread apply all bt" --args ./Editor ...` (install gdb via apt first). Full source-level frames resolve against /home/ubuntu/repos/aio3de.
- Caution: gdb `-ex "commands N ... end"` multi-line blocks in batch mode stop at the breakpoint and dump/exit instead of continuing — prefer simple breakpoints or a gdb script file.
- No Vulkan device on this box (vkCreateInstance ERROR_INCOMPATIBLE_DRIVER) — RHI-specific (GPU) crash paths cannot be exercised; only null RHI. State this limitation explicitly in reports.

## Gotchas
- xterm may not auto-scroll; send `shift+End` before screenshotting, and `shift+Prior` to show scrollback evidence.
- Launcher piped through `tail` buffers output until exit; capture to a file for grep-able evidence.
- After the missing-assetcatalog actionable error, the launcher currently continues into teardown and aborts (exit 134) with RPI/RHI errors — the actionable message still appears first.
