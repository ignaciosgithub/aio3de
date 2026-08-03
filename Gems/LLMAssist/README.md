# LLM Assist Gem

In-Editor AI assistant backed by **OpenAI**, **Anthropic** or **Kimi
(Moonshot)**, plus a one-click **Gem Manager**. Script-only gem — no C++
build; enable it and restart the Editor.

## Enable

```
scripts\o3de.bat enable-gem -gn LLMAssist -pp <your project path>
```

Requires the `EditorPythonBindings` and `QtForPython` gems (on by default in
the Editor). Restart the Editor; two new panes appear in **Tools**:

## Tools > AI Assistant

- **Chat tab** — pick a provider (openai / anthropic / kimi) and model
  (defaults: `gpt-4o`, `claude-opus-4-6`, `moonshot-v1-32k`), ask anything.
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
- **Asset/Tool gems** → just restart the Editor / Asset Processor.

## Scripting API

The backend is plain Python — usable from any Editor script:

```python
from llmassist import providers
reply = providers.chat("anthropic", [{"role": "user", "content": "hi"}])
```
