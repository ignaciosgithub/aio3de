"""
Copyright (c) Contributors to the Open 3D Engine Project.
For complete copyright and license terms please see the LICENSE at the root of this distribution.

SPDX-License-Identifier: Apache-2.0 OR MIT
"""
# User-save priority for AI file edits. The assistant never overwrites a file
# blindly:
#  - it snapshots the file's mtime/content hash when an edit is proposed;
#  - before writing, if the file changed on disk since the snapshot (the user
#    saved it), the write is refused and the caller must re-propose against the
#    new content;
#  - the UI asks the user to save and close the file in any external editor /
#    Editor tool before applying, and a timestamped .bak backup is written next
#    to the file so nothing is ever lost.

import hashlib
import os
import time


class StaleFileError(RuntimeError):
    """The file changed on disk since the AI read it (user save wins)."""


def _digest(path):
    if not os.path.isfile(path):
        return None
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


class ProposedEdit:
    """An AI-proposed replacement for one file, applied only if the user's copy
    hasn't changed since the proposal was made."""

    def __init__(self, path, new_content):
        self.path = os.path.abspath(path)
        self.new_content = new_content
        self.snapshot_digest = _digest(self.path)
        self.exists = self.snapshot_digest is not None

    def is_stale(self):
        return _digest(self.path) != self.snapshot_digest

    def current_content(self):
        if not os.path.isfile(self.path):
            return ""
        with open(self.path, "r", encoding="utf-8", errors="replace") as f:
            return f.read()

    def apply(self):
        """Write the edit. Raises StaleFileError if the user saved the file since
        the proposal — the user's version always takes priority."""
        if self.is_stale():
            raise StaleFileError(
                f"'{self.path}' changed on disk since the AI read it. The user's "
                "save takes priority: re-run the request so the AI works from the "
                "latest content.")
        backup = None
        if self.exists:
            backup = f"{self.path}.{time.strftime('%Y%m%d-%H%M%S')}.bak"
            with open(self.path, "rb") as src, open(backup, "wb") as dst:
                dst.write(src.read())
        os.makedirs(os.path.dirname(self.path) or ".", exist_ok=True)
        with open(self.path, "w", encoding="utf-8", newline="") as f:
            f.write(self.new_content)
        self.snapshot_digest = _digest(self.path)
        return backup
