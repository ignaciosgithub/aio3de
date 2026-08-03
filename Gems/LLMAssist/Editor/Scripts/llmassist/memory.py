"""
Copyright (c) Contributors to the Open 3D Engine Project.
For complete copyright and license terms please see the LICENSE at the root of this distribution.

SPDX-License-Identifier: Apache-2.0 OR MIT
"""
# Per-project persistent memory for the assistant. Stored at
# <project>/user/llmassist_memory.json — the `user` folder is per-machine and
# git-ignored, so memory persists across Editor restarts without ever being
# committed. Two kinds of entries:
#  - facts: durable notes ("the player prefab is Prefabs/NetPlayer.prefab"),
#    added explicitly or by asking the AI to "remember" something;
#  - recent exchanges: a rolling window of past question/answer pairs so the
#    assistant keeps continuity between sessions.

import json
import os
import time

_MAX_FACTS = 200
_MAX_EXCHANGES = 40
_MAX_EXCHANGE_CHARS = 1500


def _project_root():
    try:
        import azlmbr.paths
        return azlmbr.paths.projectroot
    except Exception:
        return os.getcwd()


def _memory_path(project_root=None):
    return os.path.join(project_root or _project_root(), "user", "llmassist_memory.json")


def load(project_root=None):
    path = _memory_path(project_root)
    if not os.path.isfile(path):
        return {"facts": [], "exchanges": []}
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError):
        return {"facts": [], "exchanges": []}
    return {
        "facts": list(data.get("facts", []))[:_MAX_FACTS],
        "exchanges": list(data.get("exchanges", []))[-_MAX_EXCHANGES:],
    }


def _save(data, project_root=None):
    path = _memory_path(project_root)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)


def add_fact(text, project_root=None):
    text = text.strip()
    if not text:
        return
    data = load(project_root)
    if text not in data["facts"]:
        data["facts"].append(text)
        data["facts"] = data["facts"][-_MAX_FACTS:]
        _save(data, project_root)


def remove_fact(index, project_root=None):
    data = load(project_root)
    if 0 <= index < len(data["facts"]):
        data["facts"].pop(index)
        _save(data, project_root)


def record_exchange(question, answer, project_root=None):
    data = load(project_root)
    data["exchanges"].append({
        "time": time.strftime("%Y-%m-%d %H:%M"),
        "q": question[:_MAX_EXCHANGE_CHARS],
        "a": answer[:_MAX_EXCHANGE_CHARS],
    })
    data["exchanges"] = data["exchanges"][-_MAX_EXCHANGES:]
    _save(data, project_root)


def clear(project_root=None):
    _save({"facts": [], "exchanges": []}, project_root)


def context_block(project_root=None, max_exchanges=8):
    """Format the project memory for the system prompt ('' when empty)."""
    data = load(project_root)
    parts = []
    if data["facts"]:
        parts.append("Project facts the user asked to remember:")
        parts.extend(f"- {fact}" for fact in data["facts"])
    if data["exchanges"]:
        parts.append("Recent conversation in this project (oldest first):")
        for exchange in data["exchanges"][-max_exchanges:]:
            parts.append(f"[{exchange['time']}] user: {exchange['q']}")
            parts.append(f"assistant: {exchange['a']}")
    return "\n".join(parts)
