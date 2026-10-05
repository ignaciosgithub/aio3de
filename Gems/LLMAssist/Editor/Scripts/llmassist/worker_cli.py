"""
Copyright (c) Contributors to the Open 3D Engine Project.
For complete copyright and license terms please see the LICENSE at the root of this distribution.

SPDX-License-Identifier: Apache-2.0 OR MIT
"""
# Out-of-process entry point for the AI Assistant's network calls.
#
# The Editor's embedded interpreter keeps the GIL on the main thread whenever it
# is idle in the Qt event loop, so a Python worker thread inside the Editor only
# advances while the UI happens to run Python code. Provider requests therefore
# run in a short-lived interpreter process instead: the UI starts it with
# QProcess, hands it one JSON request on stdin and reads one JSON reply from
# stdout, all without ever blocking or crashing the Editor.
#
#   python -m llmassist.worker_cli < '{"op": "chat", "provider": "openai", ...}'
#   -> {"ok": true, "result": "..."} | {"ok": false, "error": "..."}

import json
import sys

from . import providers


def handle(request):
    op = request.get("op")
    provider = request.get("provider", "")
    if op == "chat":
        return providers.chat(provider, request.get("messages", []), model=request.get("model") or None)
    if op == "models":
        return providers.fetch_models(provider)
    raise providers.LlmError(f"Unknown worker op '{op}'")


def main():
    try:
        request = json.loads(sys.stdin.read() or "{}")
        result = handle(request)
        response = {"ok": True, "result": result}
    except Exception as e:  # reported to the pane; the worker itself must always answer
        response = {"ok": False, "error": f"{e}"}
    sys.stdout.write(json.dumps(response))
    sys.stdout.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
