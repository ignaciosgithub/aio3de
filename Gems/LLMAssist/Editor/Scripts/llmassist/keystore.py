"""
Copyright (c) Contributors to the Open 3D Engine Project.
For complete copyright and license terms please see the LICENSE at the root of this distribution.

SPDX-License-Identifier: Apache-2.0 OR MIT
"""
# Per-user API key storage. Keys live OUTSIDE the project and the engine tree
# (~/.o3de/llmassist_keys.json, chmod 600 on POSIX) so they can never be
# committed to source control. Environment variables take priority so CI /
# power users can avoid the file entirely.

import json
import os
import stat

_ENV_VARS = {
    "openai": "OPENAI_API_KEY",
    "anthropic": "ANTHROPIC_API_KEY",
    "kimi": "MOONSHOT_API_KEY",
}


def _store_path():
    return os.path.join(os.path.expanduser("~"), ".o3de", "llmassist_keys.json")


def _load():
    path = _store_path()
    if not os.path.isfile(path):
        return {}
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def get_key(provider):
    """Resolve the API key for a provider: environment variable first, then the
    per-user key file. Returns '' when not configured."""
    env = _ENV_VARS.get(provider)
    if env and os.environ.get(env):
        return os.environ[env]
    return _load().get(provider, "")


def set_key(provider, key):
    """Store (or clear, with an empty key) a provider key in the per-user file."""
    data = _load()
    if key:
        data[provider] = key
    else:
        data.pop(provider, None)
    path = _store_path()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
    try:
        os.chmod(path, stat.S_IRUSR | stat.S_IWUSR)  # 600
    except OSError:
        pass  # best effort (Windows)


def key_source(provider):
    """'env', 'file' or '' — where the active key comes from."""
    env = _ENV_VARS.get(provider)
    if env and os.environ.get(env):
        return "env"
    if _load().get(provider):
        return "file"
    return ""
