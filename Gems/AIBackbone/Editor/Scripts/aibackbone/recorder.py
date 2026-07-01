"""
Copyright (c) Contributors to the Open 3D Engine Project.
For complete copyright and license terms please see the LICENSE at the root of this distribution.

SPDX-License-Identifier: Apache-2.0 OR MIT
"""
# In-engine dataset recording: sample values from gameplay/editor Python scripts
# into a JSONL dataset file that the trainer (torch_backend.py) consumes.
#
# Example (from any engine Python script):
#     from aibackbone.recorder import DatasetRecorder
#     rec = DatasetRecorder("AIModels/npc_decisions.jsonl")
#     rec.record(
#         inputs={"self_position": [x, y, z], "health": hp, "target_visible": visible},
#         outputs={"attack_score": 1.0, "flee_score": 0.0, "idle_score": 0.0})
#     rec.close()
#
# Field names/types must match the model spec's input/output variables.

import json
import os
import threading


class DatasetRecorder:
    """Appends samples to a JSONL dataset file. Thread-safe; flushes on every write
    so recorded data survives crashes."""

    def __init__(self, path, append=True):
        self.path = path
        os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
        self._lock = threading.Lock()
        self._file = open(path, "a" if append else "w", encoding="utf-8")
        self._count = 0

    def record(self, inputs, outputs):
        """Records one training sample: dicts of input fields and expected output fields."""
        line = json.dumps({"inputs": _plain(inputs), "outputs": _plain(outputs)})
        with self._lock:
            self._file.write(line + "\n")
            self._file.flush()
            self._count += 1

    def sample_count(self):
        with self._lock:
            return self._count

    def close(self):
        with self._lock:
            if not self._file.closed:
                self._file.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


def _plain(fields):
    """Converts field values (including math vector-like objects) to JSON-friendly types."""
    result = {}
    for name, value in fields.items():
        if isinstance(value, (int, float, bool)):
            result[name] = value
        elif isinstance(value, (list, tuple)):
            result[name] = [float(v) for v in value]
        elif hasattr(value, "x") and hasattr(value, "y"):
            components = [float(value.x), float(value.y)]
            if hasattr(value, "z"):
                components.append(float(value.z))
            if hasattr(value, "w"):
                components.append(float(value.w))
            result[name] = components
        else:
            result[name] = float(value)
    return result


def dataset_info(path):
    """Returns (sample_count, field_names) for a JSONL dataset without loading it fully."""
    count = 0
    fields = None
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if fields is None:
                sample = json.loads(line)
                fields = {
                    "inputs": sorted(sample.get("inputs", {}).keys()),
                    "outputs": sorted(sample.get("outputs", {}).keys()),
                }
            count += 1
    return count, (fields or {"inputs": [], "outputs": []})
