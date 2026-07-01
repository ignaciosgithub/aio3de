#!/bin/bash
# Installs the AI Backbone ML libraries (requirements-ai.txt) into the engine's
# Python environment. Run once (and again after editing requirements-ai.txt).
#
# Usage:
#   ./install_ai_libs.sh          - CPU PyTorch (works everywhere)
#   ./install_ai_libs.sh --cuda   - CUDA PyTorch (NVIDIA GPUs, cu128 wheels; supports RTX 50-series)

set -e

GEM_DIR="$(cd "$(dirname "$0")" && pwd)"
ENGINE_PIP="$GEM_DIR/../../python/pip.sh"

if [ ! -f "$ENGINE_PIP" ]; then
    echo "[AIBackbone] Could not find the engine pip wrapper at $ENGINE_PIP"
    exit 1
fi

if [ "$1" == "--cuda" ]; then
    echo "[AIBackbone] Installing ML libraries with CUDA PyTorch..."
    "$ENGINE_PIP" install --index-url https://download.pytorch.org/whl/cu128 torch
fi

echo "[AIBackbone] Installing ML libraries from requirements-ai.txt..."
"$ENGINE_PIP" install -r "$GEM_DIR/requirements-ai.txt"

echo "[AIBackbone] Done. Restart the Editor and open Tools -> AI Model Builder."
