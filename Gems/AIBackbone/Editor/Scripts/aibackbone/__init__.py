"""
Copyright (c) Contributors to the Open 3D Engine Project.
For complete copyright and license terms please see the LICENSE at the root of this distribution.

SPDX-License-Identifier: Apache-2.0 OR MIT
"""
# AI Backbone: in-engine neural network creation, training, import and data recording.

__version__ = "0.1.0"


def ml_available():
    """Returns (available: bool, message: str) describing whether the ML libraries are installed."""
    missing = []
    for lib in ("numpy", "torch", "onnx", "onnxruntime"):
        try:
            __import__(lib)
        except ImportError:
            missing.append(lib)
    if missing:
        return False, (
            "Missing Python libraries: " + ", ".join(missing) +
            ". Run install_ai_libs (in Gems/AIBackbone) and restart the Editor."
        )
    return True, "OK"
