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
import ssl
import urllib.request
import urllib.error

from . import keystore

# Built-in model lineup per provider (first entry = default). The UI's Refresh
# button replaces this with the live list from the provider's /models endpoint
# (cached in ~/.o3de/llmassist_models_cache.json), and users can pin extra ids
# via ~/.o3de/llmassist_models.json:
#   { "openai": ["some-new-model"], "anthropic": [...], "kimi": [...] }
# User-added models are listed first, and any model id can also be typed
# directly in the UI's editable model box.
KNOWN_MODELS = {
    "openai": [
        "gpt-6-sol",
        "gpt-6-astra",
        "gpt-6-luna",
        "gpt-6.1-sol",
        "gpt-5.5",
        "gpt-5.4",
        "gpt-5.4-mini",
        "gpt-5.4-nano",
        "gpt-5",
        "gpt-5-mini",
        "gpt-4.1",
        "gpt-4.1-mini",
        "o3",
        "o4-mini",
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


def _models_cache_path():
    return os.path.join(os.path.expanduser("~"), ".o3de", "llmassist_models_cache.json")


def cached_remote_models(provider):
    """Model ids last fetched from the provider (see fetch_models), or []."""
    path = _models_cache_path()
    if not os.path.isfile(path):
        return []
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError):
        return []
    entries = data.get(provider, []) if isinstance(data, dict) else []
    return [str(m) for m in entries if m] if isinstance(entries, list) else []


def store_remote_models(provider, models):
    path = _models_cache_path()
    data = {}
    if os.path.isfile(path):
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
        except (OSError, ValueError):
            data = {}
    if not isinstance(data, dict):
        data = {}
    data[provider] = list(models)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)


def models_for(provider):
    """User-added models, then the provider's live list if it was ever fetched,
    then the built-in lineup (deduplicated)."""
    merged = list(user_models().get(provider, []))
    for model in cached_remote_models(provider) + KNOWN_MODELS.get(provider, []):
        if model not in merged:
            merged.append(model)
    return merged


def default_model(provider):
    models = models_for(provider)
    return models[0] if models else ""


# Backwards-compatible mapping of provider -> default model id.
DEFAULT_MODELS = {provider: KNOWN_MODELS[provider][0] for provider in PROVIDERS}

_TIMEOUT_SECONDS = 120
# Reasoning models think before they answer and can take minutes on big prompts.
_REASONING_TIMEOUT_SECONDS = 600
# Reasoning tokens count against `max_completion_tokens`, so reasoning models get
# this much extra budget on top of the visible-answer budget; otherwise a hard
# question spends the whole budget thinking and the answer comes back empty.
_REASONING_HEADROOM_TOKENS = 16000
# low keeps in-Editor replies responsive; override with LLMASSIST_REASONING_EFFORT
# (minimal|low|medium|high) for harder problems.
_DEFAULT_REASONING_EFFORT = "low"


class LlmError(RuntimeError):
    pass


class ProviderHttpError(LlmError):
    """Non-2xx answer from the provider; `code` and `body` allow callers to adapt."""

    def __init__(self, code, body):
        super().__init__(f"HTTP {code} from provider: {body[:500]}")
        self.code = code
        self.body = body


def _send_json(request, timeout):
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        detail = ""
        try:
            detail = e.read().decode("utf-8", errors="replace")
        except OSError:
            pass
        raise ProviderHttpError(e.code, detail) from e
    except urllib.error.URLError as e:
        if isinstance(e.reason, ssl.SSLCertVerificationError):
            raise LlmError(
                f"TLS certificate verification failed: {e.reason}. The Editor's bundled "
                "Python found no system CA certificates; point SSL_CERT_FILE at your "
                "distribution's CA bundle (e.g. /etc/ssl/certs/ca-certificates.crt) "
                "before launching the Editor.") from e
        raise LlmError(f"Network error contacting provider: {e.reason}") from e
    except TimeoutError as e:
        raise LlmError(f"Provider did not answer within {timeout}s.") from e


def _post_json(url, headers, payload, timeout=_TIMEOUT_SECONDS):
    body = json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(url, data=body, method="POST")
    request.add_header("Content-Type", "application/json")
    for name, value in headers.items():
        request.add_header(name, value)
    return _send_json(request, timeout)


def _get_json(url, headers, timeout=_TIMEOUT_SECONDS):
    request = urllib.request.Request(url, method="GET")
    for name, value in headers.items():
        request.add_header(name, value)
    return _send_json(request, timeout)


def _reasoning_effort():
    effort = os.environ.get("LLMASSIST_REASONING_EFFORT", "").strip().lower()
    return effort if effort in ("minimal", "low", "medium", "high") else _DEFAULT_REASONING_EFFORT


# Chat-completions parameters differ between OpenAI's model generations: models
# from gpt-5 on (and the o-series) reject `max_tokens` / non-default temperature
# and take `max_completion_tokens` + `reasoning_effort`; gpt-4-era models reject
# `reasoning_effort`. The 400 body names the offending parameter, so a request
# built for the wrong generation is retried once with the other parameter set.
_LEGACY_ONLY_PARAMS = ("max_tokens", "temperature")
_REASONING_ONLY_PARAMS = ("reasoning_effort", "max_completion_tokens")


def _unsupported_parameter(error):
    """Name of the request parameter a 400 response complains about, or ''."""
    if error.code != 400:
        return ""
    try:
        detail = json.loads(error.body).get("error", {})
    except ValueError:
        detail = {}
    param = detail.get("param") if isinstance(detail, dict) else None
    if isinstance(param, str) and param:
        return param
    message = detail.get("message", "") if isinstance(detail, dict) else ""
    for name in _LEGACY_ONLY_PARAMS + _REASONING_ONLY_PARAMS:
        if name in message and "nsupported" in message:
            return name
    return ""


def _is_legacy_openai_model(model):
    return model.startswith(("gpt-4", "gpt-3", "chatgpt"))


def _completion_payload(model, messages, max_tokens, temperature, reasoning_style):
    payload = {"model": model, "messages": messages}
    if reasoning_style:
        payload["max_completion_tokens"] = max_tokens + _REASONING_HEADROOM_TOKENS
        payload["reasoning_effort"] = _reasoning_effort()
    else:
        payload["max_tokens"] = max_tokens
        payload["temperature"] = temperature
    return payload


def _chat_openai_style(base_url, key, model, messages, max_tokens, temperature,
                       reasoning_style=False, provider_label="OpenAI"):
    url = base_url + "/chat/completions"
    headers = {"Authorization": f"Bearer {key}"}

    def post(style):
        timeout = _REASONING_TIMEOUT_SECONDS if style else _TIMEOUT_SECONDS
        payload = _completion_payload(model, messages, max_tokens, temperature, style)
        return _post_json(url, headers, payload, timeout)

    try:
        data = post(reasoning_style)
    except ProviderHttpError as e:
        if e.code == 404 and "model_not_found" in e.body:
            raise LlmError(
                f"{provider_label} has no model called '{model}' (or your key cannot use it). "
                "Press the refresh button next to the model box to list the models your "
                "key can access, or type an exact id.") from e
        param = _unsupported_parameter(e)
        if param in _LEGACY_ONLY_PARAMS and not reasoning_style:
            data = post(True)
        elif param in _REASONING_ONLY_PARAMS and reasoning_style:
            data = post(False)
        else:
            raise
    try:
        choice = data["choices"][0]
        message = choice["message"]
        content = message.get("content")
        finish_reason = choice.get("finish_reason")
    except (KeyError, IndexError, TypeError) as e:
        raise LlmError(f"Unexpected response shape: {str(data)[:300]}") from e
    if content:
        return content
    refusal = message.get("refusal")
    if refusal:
        raise LlmError(f"The model refused to answer: {refusal}")
    if finish_reason == "length":
        raise LlmError(
            f"'{model}' used its whole token budget before producing an answer "
            "(reasoning models spend tokens thinking first). Ask a narrower question, "
            "or pick a non-reasoning model such as gpt-4.1.")
    raise LlmError(f"Empty reply from provider (finish_reason={finish_reason}).")


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
        return _chat_openai_style("https://api.openai.com/v1", key, model, messages,
                                  max_tokens, temperature,
                                  reasoning_style=not _is_legacy_openai_model(model))
    if provider == "kimi":
        return _chat_openai_style("https://api.moonshot.ai/v1", key, model, messages,
                                  max_tokens, temperature, provider_label="Moonshot")
    return _chat_anthropic(key, model, messages, max_tokens, temperature)


# Families listed by OpenAI's /models that are not chat models.
_NON_CHAT_MARKERS = (
    "embedding", "tts", "whisper", "transcribe", "dall-e", "image", "audio",
    "realtime", "moderation", "babbage", "davinci", "instruct", "search-api",
    "deep-research", "computer-use", "sora", "omni", "-codex", "codex-",
)


def _is_chat_model(model_id):
    return not any(marker in model_id for marker in _NON_CHAT_MARKERS)


def fetch_models(provider):
    """Ask the provider which models the configured key can use; newest first.
    The result is cached so it survives Editor restarts (see models_for)."""
    if provider not in PROVIDERS:
        raise LlmError(f"Unknown provider '{provider}' (expected one of {PROVIDERS})")
    key = keystore.get_key(provider)
    if not key:
        raise LlmError(
            f"No API key configured for '{provider}'. Add one in the Settings tab "
            "of the AI Assistant (Tools > AI Assistant).")
    if provider == "anthropic":
        data = _get_json("https://api.anthropic.com/v1/models?limit=1000",
                         {"x-api-key": key, "anthropic-version": "2023-06-01"})
    elif provider == "kimi":
        data = _get_json("https://api.moonshot.ai/v1/models", {"Authorization": f"Bearer {key}"})
    else:
        data = _get_json("https://api.openai.com/v1/models", {"Authorization": f"Bearer {key}"})
    try:
        # OpenAI/Moonshot: `created` epoch seconds; Anthropic: `created_at` ISO-8601.
        entries = [(str(m["id"]), str(m.get("created") or m.get("created_at") or ""))
                   for m in data["data"]]
    except (KeyError, TypeError) as e:
        raise LlmError(f"Unexpected model list shape: {str(data)[:300]}") from e
    if provider == "openai":
        entries = [(mid, created) for mid, created in entries if _is_chat_model(mid)]
    entries.sort(key=lambda item: item[0])
    entries.sort(key=lambda item: item[1].rjust(20, "0"), reverse=True)
    models = [mid for mid, _ in entries]
    store_remote_models(provider, models)
    return models
