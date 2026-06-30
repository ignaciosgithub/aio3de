#!/bin/bash

#
# Copyright (c) Contributors to the Open 3D Engine Project.
# For complete copyright and license terms please see the LICENSE at the root of this distribution.
#
# SPDX-License-Identifier: Apache-2.0 OR MIT
#
#

# Launches the cross-platform Tkinter GUI hub. This deliberately uses the SYSTEM Python (not the
# engine's bundled venv) so it works on a fresh clone before the bundled Python has been set up -
# the whole point of the hub is to help you get to that point.

SOURCE="${BASH_SOURCE[0]}"
while [[ -h "$SOURCE" ]]; do
    DIR="$( cd -P "$( dirname "$SOURCE" )" && pwd )"
    SOURCE="$( readlink "$SOURCE" )"
    [[ $SOURCE != /* ]] && SOURCE="$DIR/$SOURCE"
done
SCRIPT_DIR="$( cd -P "$( dirname "$SOURCE" )" && pwd )"

PYTHON_BIN=""
for candidate in python3 python; do
    if command -v "$candidate" >/dev/null 2>&1; then
        PYTHON_BIN="$candidate"
        break
    fi
done

if [[ -z "$PYTHON_BIN" ]]; then
    echo "No system Python found on PATH. Install Python 3.10+ to use the GUI hub."
    exit 1
fi

exec "$PYTHON_BIN" "$SCRIPT_DIR/o3de_hub_gui.py" "$@"
