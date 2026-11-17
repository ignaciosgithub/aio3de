#!/bin/bash

# 
# Copyright (c) Contributors to the Open 3D Engine Project.
# For complete copyright and license terms please see the LICENSE at the root of this distribution.
# 
# SPDX-License-Identifier: Apache-2.0 OR MIT
# 

# Usage: install_ai_libs.sh [--torch BUILD] [--torch-version X.Y.Z] [--pinned] [--all] [--check]
#        --cuda is an alias for --torch cu128.

GEM_DIR="$(cd "$(dirname "$0")" && pwd)"
ENGINE_PYTHON="$GEM_DIR/../../python/python.sh"

if [ ! -f "$ENGINE_PYTHON" ]; then
    echo "[AIBackbone] Could not find the engine Python wrapper at $ENGINE_PYTHON"
    exit 1
fi

export PYTHONPATH="$GEM_DIR/Editor/Scripts${PYTHONPATH:+:$PYTHONPATH}"
exec "$ENGINE_PYTHON" -c 'import runpy,sys; sys.path.insert(0,sys.argv.pop(1)); sys.argv[0]="aibackbone.deps"; runpy.run_module("aibackbone.deps",run_name="__main__")' "$GEM_DIR/Editor/Scripts" "$@"
