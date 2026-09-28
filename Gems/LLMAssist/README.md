# LLM Assist Gem

In-Editor AI assistant backed by **OpenAI**, **Anthropic** or **Kimi
(Moonshot)**, plus a one-click **Gem Manager**. Script-only gem — no C++
build; enable it, re-run the CMake configure and restart the Editor.

## Enable

```
scripts\o3de.bat enable-gem -gn LLMAssist -pp <your project path>   (Windows)
scripts/o3de.sh  enable-gem -gn LLMAssist -pp <your project path>   (Linux/macOS)
```

Then re-run the CMake configure for your project (e.g. `cmake -B build/linux -S .`
in the project folder, or the hub's **Configure** button) — that step writes the
gem into the `cmake_dependencies.*.setreg` the Editor reads to know which gems
are active; no compile is needed. Requires the `EditorPythonBindings` and
`QtForPython` gems (on by default in the Editor). Restart the Editor; two new
panes appear in **Tools** and the Editor console prints
`LLMAssist: registered AI Assistant and Gem Manager view panes (Tools menu).`

## Tools > AI Assistant

- **Chat tab** — pick a provider (openai / anthropic / kimi) and a model from
  the dropdown (defaults: `gpt-5`, `claude-opus-4-6`, `kimi-k2-0905-preview`;
  the full current lineup of each provider is listed). The box is editable —
  type any model id directly — and the **+** button saves it to your personal
  list in `~/.o3de/llmassist_models.json`, so newly released models can be
  added without any engine update.
- **Docs-aware**: with the checkbox on (default), the assistant is given the
  most relevant sections of the engine's documentation
  (`docs/aio3de/*.md`, gem READMEs) **and the recent engine updates** (git
  log), so it answers about *this fork* specifically instead of generic O3DE.
- **File edits with user-save priority**: when a reply contains
  `FILE: <path>` + a code block, the *Apply file edits* button activates. For
  each file the assistant:
  1. asks you to **save and close** the file anywhere it's open — your save
     always takes priority;
  2. refuses to write if the file changed on disk since the AI read it
     (re-ask so it works from your latest content);
  3. writes a timestamped `.bak` backup next to the file before applying.
- **Scene-aware + scene actions**: with the checkbox on (default), the
  assistant sees the open level — every entity as `name [id] (parent)` plus
  the components and property paths/values of the **selected** entities — and
  can propose changes as an `actions` block:

  ````text
  ```actions
  [
    {"action": "create_entity", "name": "Crate", "parent": "Props", "position": [0, 0, 2]},
    {"action": "add_component", "entity": "Crate", "component": "PhysX Dynamic Rigid Body"},
    {"action": "set_property", "entity": "Crate", "component": "PhysX Dynamic Rigid Body",
     "property": "Mass", "value": 25},
    {"action": "select", "entities": ["Crate"]},
    {"action": "rebuild_csharp"}
  ]
  ```
  ````

  The *Apply scene actions* button shows the full list and asks once before
  running it; destructive actions (`delete_entity`, `remove_component`,
  `save_level`, `run_console`) are confirmed one by one. Everything runs
  inside a single undo batch — **Ctrl+Z reverts the whole reply** — and stops
  at the first failure (the transcript shows what was applied / why it
  stopped). Supported actions: `create_entity`, `delete_entity`,
  `rename_entity`, `set_parent`, `select`, `set_transform` (position /
  rotation_degrees / uniform scale), `add_component`, `remove_component`,
  `set_property` (numbers, bools, strings, `[x,y,z]` vectors, colors,
  entity names for entity-reference properties), `rebuild_csharp` (runs
  `csharp_rebuild`), `run_console`, `save_level`. Entities are referenced by
  name (case-insensitive) or by the `[id]` from the listing when names
  repeat; component and property names may be partial as long as they are
  unambiguous. Lua scripts need no rebuild — edited `.lua` files are picked up
  by the Asset Processor. Script *source* changes still go through `FILE:`
  edits; both kinds can appear in the same reply.
- **Memory tab — per-project persistent memory**: the assistant remembers
  across Editor restarts, per project. Durable **facts** (add them in the tab
  or type `remember: <fact>` in the chat) are always included in its context,
  and a rolling window of recent exchanges keeps conversational continuity.
  Stored in `<project>/user/llmassist_memory.json` (the `user/` folder is
  git-ignored, so memory never gets committed). Inspect, edit or clear it any
  time from the tab.
- **Settings tab** — enter API keys per provider. Keys are stored per-user in
  `~/.o3de/llmassist_keys.json` (chmod 600), **outside the project and the
  engine tree, never committed**. Environment variables (`OPENAI_API_KEY`,
  `ANTHROPIC_API_KEY`, `MOONSHOT_API_KEY`) take priority over the file.

## Tools > Gem Manager

Enable/disable gems without the command line: a searchable list of every gem
shipped with the engine, with a checkbox per gem. Toggles go through the
official `o3de enable-gem`/`disable-gem` CLI so `project.json` stays
canonical, and the panel tells you what's needed afterwards:

- **Code gems** → re-run CMake configure → rebuild the Editor → relaunch.
- **Asset/Tool gems** → re-run CMake configure (no compile) → restart the
  Editor / Asset Processor.

## Scripting API

The backend is plain Python — usable from any Editor script:

```python
from llmassist import providers
reply = providers.chat("anthropic", [{"role": "user", "content": "hi"}])

from llmassist import scene_actions
backend = scene_actions.EditorBackend()          # azlmbr-backed, Editor only
print(scene_actions.scene_context(backend))      # what the assistant sees
actions = scene_actions.parse_actions(reply)     # ```actions blocks -> list
scene_actions.Executor(backend).run(actions)     # (applied, messages)
```

Unit tests (no Editor needed): `python -m unittest discover -s Gems/LLMAssist/Tests`.
