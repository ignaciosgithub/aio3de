#!/bin/bash

# 
# Copyright (c) Contributors to the Open 3D Engine Project.
# For complete copyright and license terms please see the LICENSE at the root of this distribution.
# 
# SPDX-License-Identifier: Apache-2.0 OR MIT
# 

GEM_DIR="$(cd "$(dirname "$0")" && pwd)"
ENGINE_PYTHON="$GEM_DIR/../../python/python.sh"

if [ ! -f "$ENGINE_PYTHON" ]; then
    echo "[AIBackbone] Could not find the engine Python wrapper at $ENGINE_PYTHON"
    exit 1
fi

ARGS=()
for arg in "$@"; do
    if [ "$arg" = "--cuda" ]; then
        ARGS+=(--torch cu128)
    else
        ARGS+=("$arg")
    fi
done

export PYTHONPATH="$GEM_DIR/Editor/Scripts${PYTHONPATH:+:$PYTHONPATH}"
exec "$ENGINE_PYTHON" -c 'import runpy,sys; sys.path.insert(0,sys.argv.pop(1)); sys.argv[0]="aibackbone.deps"; runpy.run_module("aibackbone.deps",run_name="__main__")' "$GEM_DIR/Editor/Scripts" "${ARGS[@]}"
