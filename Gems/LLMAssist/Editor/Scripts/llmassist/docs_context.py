"""
Copyright (c) Contributors to the Open 3D Engine Project.
For complete copyright and license terms please see the LICENSE at the root of this distribution.

SPDX-License-Identifier: Apache-2.0 OR MIT
"""
# Makes the assistant docs-aware: gathers the engine's documentation
# (docs/aio3de/*.md, gem READMEs) plus recent updates (git log of the engine
# repo), then selects the most relevant sections for a question with a simple
# keyword-overlap score so the context fits in the prompt budget.

import os
import re
import subprocess

_MAX_CONTEXT_CHARS = 24000
_MAX_SECTION_CHARS = 4000
_MAX_LOG_ENTRIES = 30

_GEM_READMES = (
    "Gems/ArenaShooter/README.md",
    "Gems/NeuralBots/README.md",
    "Gems/ArenaShooterNet/README.md",
    "Gems/AIBackbone/README.md",
)


def engine_root():
    try:
        import azlmbr.paths
        return azlmbr.paths.engroot
    except Exception:
        # Fallback: this file lives at <engine>/Gems/LLMAssist/Editor/Scripts/llmassist/
        return os.path.abspath(os.path.join(os.path.dirname(__file__), *[".."] * 5))


def _doc_files(root):
    files = []
    docs_dir = os.path.join(root, "docs", "aio3de")
    if os.path.isdir(docs_dir):
        files.extend(
            os.path.join(docs_dir, f) for f in sorted(os.listdir(docs_dir)) if f.endswith(".md"))
    for rel in _GEM_READMES:
        path = os.path.join(root, rel)
        if os.path.isfile(path):
            files.append(path)
    return files


def _split_sections(path):
    """Split a markdown file into (title, body) sections on ## headings."""
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            text = f.read()
    except OSError:
        return []
    name = os.path.relpath(path, engine_root())
    sections = []
    current_title = name
    current = []
    for line in text.splitlines():
        if line.startswith("## "):
            if current:
                sections.append((current_title, "\n".join(current)))
            current_title = f"{name} — {line[3:].strip()}"
            current = []
        else:
            current.append(line)
    if current:
        sections.append((current_title, "\n".join(current)))
    return [(t, b[:_MAX_SECTION_CHARS]) for t, b in sections if b.strip()]


_WORD = re.compile(r"[a-zA-Z_][a-zA-Z0-9_]+")


def _score(question_words, title, body):
    text_words = set(w.lower() for w in _WORD.findall(title + " " + body))
    return len(question_words & text_words)


def recent_updates(root):
    """Recent engine updates from git history (best effort; '' when not a repo)."""
    try:
        out = subprocess.run(
            ["git", "-C", root, "log", f"-{_MAX_LOG_ENTRIES}", "--pretty=format:%ad %s",
             "--date=short"],
            capture_output=True, text=True, timeout=10)
        return out.stdout.strip() if out.returncode == 0 else ""
    except (OSError, subprocess.SubprocessError):
        return ""


def build_context(question):
    """Return a context string with the doc sections most relevant to the
    question plus the recent-updates log, capped to the prompt budget."""
    root = engine_root()
    question_words = set(w.lower() for w in _WORD.findall(question))
    scored = []
    for path in _doc_files(root):
        for title, body in _split_sections(path):
            scored.append((_score(question_words, title, body), title, body))
    scored.sort(key=lambda item: item[0], reverse=True)

    parts = []
    used = 0
    for score, title, body in scored:
        if score <= 0 or used >= _MAX_CONTEXT_CHARS:
            break
        chunk = f"### {title}\n{body}\n"
        parts.append(chunk)
        used += len(chunk)

    updates = recent_updates(root)
    if updates:
        parts.append("### Recent engine updates (git log)\n" + updates)
    return "\n".join(parts)


def system_prompt(question):
    context = build_context(question)
    prompt = (
        "You are the in-Editor AI assistant for aio3de, a fork of the Open 3D Engine "
        "(O3DE). Answer questions about this specific engine using the documentation "
        "excerpts below when relevant; they are authoritative for fork-specific "
        "features. If the docs do not cover something, say so rather than guessing. "
        "When proposing file changes, always show the full new file content in a "
        "fenced code block preceded by a line 'FILE: <path relative to the project "
        "or engine root>'.")
    if context:
        prompt += "\n\n--- ENGINE DOCUMENTATION EXCERPTS ---\n" + context
    return prompt
