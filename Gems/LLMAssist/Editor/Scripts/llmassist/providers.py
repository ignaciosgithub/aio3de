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
import urllib.request
import urllib.error

from . import keystore

DEFAULT_MODELS = {
    "openai": "gpt-4o",
    "anthropic": "claude-opus-4-6",
    "kimi": "moonshot-v1-32k",
}

PROVIDERS = tuple(DEFAULT_MODELS)

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


def _chat_openai_style(base_url, key, model, messages, max_tokens, temperature):
    data = _post_json(
        base_url + "/chat/completions",
        {"Authorization": f"Bearer {key}"},
        {
            "model": model,
            "messages": messages,
            "max_tokens": max_tokens,
            "temperature": temperature,
        })
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
    model = model or DEFAULT_MODELS[provider]
    if provider == "openai":
        return _chat_openai_style("https://api.openai.com/v1", key, model, messages,
                                  max_tokens, temperature)
    if provider == "kimi":
        return _chat_openai_style("https://api.moonshot.ai/v1", key, model, messages,
                                  max_tokens, temperature)
    return _chat_anthropic(key, model, messages, max_tokens, temperature)
