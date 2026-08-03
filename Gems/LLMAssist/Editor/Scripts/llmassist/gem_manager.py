"""
Copyright (c) Contributors to the Open 3D Engine Project.
For complete copyright and license terms please see the LICENSE at the root of this distribution.

SPDX-License-Identifier: Apache-2.0 OR MIT
"""
# Backend for the in-Editor Gem Manager: list every gem shipped with the
# engine together with its enabled state in the current project, and toggle
# gems through the official `o3de enable-gem` / `disable-gem` CLI (so
# project.json stays canonical). Code gems are flagged as needing a CMake
# reconfigure + Editor rebuild after a change; asset/tool gems only need an
# Editor restart.

import json
import os
import subprocess
import sys


def _engine_root():
    try:
        import azlmbr.paths
        return azlmbr.paths.engroot
    except Exception:
        return os.path.abspath(os.path.join(os.path.dirname(__file__), *[".."] * 5))


def _project_root():
    try:
        import azlmbr.paths
        return azlmbr.paths.projectroot
    except Exception:
        return os.getcwd()


def _o3de_script():
    root = _engine_root()
    if os.name == "nt":
        return os.path.join(root, "scripts", "o3de.bat")
    return os.path.join(root, "scripts", "o3de.sh")


def _read_json(path):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def enabled_gems(project_path=None):
    data = _read_json(os.path.join(project_path or _project_root(), "project.json"))
    names = data.get("gem_names", [])
    result = set()
    for entry in names:
        # entries can be strings or {"name": ..., "optional": ...} dicts
        if isinstance(entry, dict):
            entry = entry.get("name", "")
        result.add(str(entry).split("==")[0])
    return result


def list_gems(project_path=None):
    """[{name, display_name, summary, type, enabled, needs_rebuild}] for every
    gem under <engine>/Gems, sorted by name."""
    gems_dir = os.path.join(_engine_root(), "Gems")
    enabled = enabled_gems(project_path)
    gems = []
    if not os.path.isdir(gems_dir):
        return gems
    for entry in sorted(os.listdir(gems_dir), key=str.lower):
        gem_json = os.path.join(gems_dir, entry, "gem.json")
        if not os.path.isfile(gem_json):
            continue
        data = _read_json(gem_json)
        name = data.get("gem_name", entry)
        gem_type = data.get("type", "Code")
        gems.append({
            "name": name,
            "display_name": data.get("display_name", name),
            "summary": data.get("summary", ""),
            "type": gem_type,
            "enabled": name in enabled,
            # Code gems compile into the Editor; Asset/Tool gems don't.
            "needs_rebuild": gem_type == "Code",
        })
    return gems


def set_gem_enabled(name, enable, project_path=None):
    """Enable or disable a gem for the project via the o3de CLI.
    Returns (ok, message)."""
    project = project_path or _project_root()
    script = _o3de_script()
    if not os.path.isfile(script):
        return False, f"o3de CLI not found at {script}"
    command = "enable-gem" if enable else "disable-gem"
    try:
        run = subprocess.run(
            [script, command, "-gn", name, "-pp", project],
            capture_output=True, text=True, timeout=120,
            shell=False if os.name != "nt" else False)
    except (OSError, subprocess.SubprocessError) as e:
        return False, f"Failed to run o3de {command}: {e}"
    if run.returncode != 0:
        detail = (run.stderr or run.stdout or "").strip()[-500:]
        return False, f"o3de {command} failed:\n{detail}"
    return True, f"{'Enabled' if enable else 'Disabled'} {name}."
