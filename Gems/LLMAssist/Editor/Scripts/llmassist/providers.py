"""
Copyright (c) Contributors to the Open 3D Engine Project.
For complete copyright and license terms please see the LICENSE at the root of this distribution.

SPDX-License-Identifier: Apache-2.0 OR MIT
"""
# Provider-agnostic chat completion over three backends, using only the Python
# standard library (urllib) so no extra packages are required in the Editor's
# Python environment.
#
#   chat(provider, messages, ...) -> assistant text
#
# messages: list of {"role": "system"|"user"|"assistant", "content": str}

import json
import os
import urllib.request
import urllib.error

from . import keystore

# Built-in model lineup per provider (first entry = default). Users can extend
# these without touching engine code via ~/.o3de/llmassist_models.json:
#   { "openai": ["some-new-model"], "anthropic": [...], "kimi": [...] }
# User-added models are listed first, and any model id can also be typed
# directly in the UI's editable model box.
KNOWN_MODELS = {
    "openai": [
        "gpt-5",
        "gpt-5-mini",
        "gpt-5-nano",
        "gpt-4.1",
        "gpt-4.1-mini",
        "o3",
        "o4-mini",
        "gpt-4o",
    ],
    "anthropic": [
        "claude-opus-4-6",
        "claude-sonnet-4-5",
        "claude-haiku-4-5",
        "claude-opus-4-1",
    ],
    "kimi": [
        "kimi-k2-0905-preview",
        "kimi-k2-turbo-preview",
        "kimi-latest",
        "moonshot-v1-32k",
    ],
}

PROVIDERS = tuple(KNOWN_MODELS)


def _user_models_path():
    return os.path.join(os.path.expanduser("~"), ".o3de", "llmassist_models.json")


def user_models():
    """User-added models from ~/.o3de/llmassist_models.json ({provider: [ids]})."""
    path = _user_models_path()
    if not os.path.isfile(path):
        return {}
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError):
        return {}
    result = {}
    for provider in PROVIDERS:
        entries = data.get(provider, [])
        if isinstance(entries, list):
            result[provider] = [str(m) for m in entries if m]
    return result


def add_user_model(provider, model):
    """Persist a user-added model id so it appears in the dropdown from now on."""
    model = model.strip()
    if provider not in PROVIDERS or not model:
        return
    path = _user_models_path()
    data = {}
    if os.path.isfile(path):
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
        except (OSError, ValueError):
            data = {}
    models = [m for m in data.get(provider, []) if isinstance(m, str)]
    if model not in models:
        models.insert(0, model)
    data[provider] = models
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)


def models_for(provider):
    """User-added models first, then the built-in lineup (deduplicated)."""
    merged = list(user_models().get(provider, []))
    for model in KNOWN_MODELS.get(provider, []):
        if model not in merged:
            merged.append(model)
    return merged


def default_model(provider):
    models = models_for(provider)
    return models[0] if models else ""


# Backwards-compatible mapping of provider -> default model id.
DEFAULT_MODELS = {provider: KNOWN_MODELS[provider][0] for provider in PROVIDERS}

_TIMEOUT_SECONDS = 120


class LlmError(RuntimeError):
    pass


def _post_json(url, headers, payload):
    body = json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(url, data=body, method="POST")
    request.add_header("Content-Type", "application/json")
    for name, value in headers.items():
        request.add_header(name, value)
    try:
        with urllib.request.urlopen(request, timeout=_TIMEOUT_SECONDS) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        detail = ""
        try:
            detail = e.read().decode("utf-8", errors="replace")[:500]
        except OSError:
            pass
        raise LlmError(f"HTTP {e.code} from provider: {detail}") from e
    except urllib.error.URLError as e:
        raise LlmError(f"Network error contacting provider: {e.reason}") from e


def _chat_openai_style(base_url, key, model, messages, max_tokens, temperature,
                       reasoning_style=False):
    payload = {"model": model, "messages": messages}
    if reasoning_style:
        # Newer OpenAI models (gpt-5*, o-series) reject `max_tokens` and only
        # accept the default temperature.
        payload["max_completion_tokens"] = max_tokens
    else:
        payload["max_tokens"] = max_tokens
        payload["temperature"] = temperature
    data = _post_json(
        base_url + "/chat/completions",
        {"Authorization": f"Bearer {key}"},
        payload)
    try:
        return data["choices"][0]["message"]["content"]
    except (KeyError, IndexError, TypeError) as e:
        raise LlmError(f"Unexpected response shape: {str(data)[:300]}") from e


def _chat_anthropic(key, model, messages, max_tokens, temperature):
    system_parts = [m["content"] for m in messages if m["role"] == "system"]
    turns = [m for m in messages if m["role"] != "system"]
    payload = {
        "model": model,
        "max_tokens": max_tokens,
        "temperature": temperature,
        "messages": turns,
    }
    if system_parts:
        payload["system"] = "\n\n".join(system_parts)
    data = _post_json(
        "https://api.anthropic.com/v1/messages",
        {"x-api-key": key, "anthropic-version": "2023-06-01"},
        payload)
    try:
        return "".join(block.get("text", "") for block in data["content"])
    except (KeyError, TypeError) as e:
        raise LlmError(f"Unexpected response shape: {str(data)[:300]}") from e


def chat(provider, messages, model=None, max_tokens=2048, temperature=0.3):
    """Run one chat completion against the chosen provider. Raises LlmError with
    a readable message on any failure (missing key, network, API error)."""
    if provider not in PROVIDERS:
        raise LlmError(f"Unknown provider '{provider}' (expected one of {PROVIDERS})")
    key = keystore.get_key(provider)
    if not key:
        raise LlmError(
            f"No API key configured for '{provider}'. Add one in the Settings tab "
            "of the AI Assistant (Tools > AI Assistant).")
    model = model or default_model(provider)
    if provider == "openai":
        reasoning_style = model.startswith(("gpt-5", "o1", "o3", "o4"))
        return _chat_openai_style("https://api.openai.com/v1", key, model, messages,
                                  max_tokens, temperature, reasoning_style)
    if provider == "kimi":
        return _chat_openai_style("https://api.moonshot.ai/v1", key, model, messages,
                                  max_tokens, temperature)
    return _chat_anthropic(key, model, messages, max_tokens, temperature)
