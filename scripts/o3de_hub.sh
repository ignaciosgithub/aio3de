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

# A usable Python is 3.10+ with Tkinter (Ubuntu/Debian/Fedora ship Tkinter as a separate package).
python_ok() {
    "$1" -c 'import sys, tkinter; sys.exit(0 if sys.version_info >= (3, 10) else 1)' >/dev/null 2>&1
}

find_python() {
    for candidate in python3 python; do
        if command -v "$candidate" >/dev/null 2>&1 && python_ok "$candidate"; then
            PYTHON_BIN="$candidate"
            return 0
        fi
    done
    return 1
}

install_python_tk() {
    local install_cmd
    if command -v apt-get >/dev/null 2>&1; then
        install_cmd="apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y python3 python3-tk"
    elif command -v dnf >/dev/null 2>&1; then
        install_cmd="dnf install -y python3 python3-tkinter"
    elif command -v pacman >/dev/null 2>&1; then
        install_cmd="pacman -S --needed --noconfirm python tk"
    else
        return 1
    fi
    echo "Installing Python 3 + Tkinter for the GUI hub: $install_cmd"
    if [[ $EUID -eq 0 ]]; then
        sh -c "$install_cmd"
    elif command -v sudo >/dev/null 2>&1; then
        sudo sh -c "$install_cmd"
    elif command -v pkexec >/dev/null 2>&1; then
        pkexec sh -c "$install_cmd"
    else
        return 1
    fi
}

PYTHON_BIN=""
if ! find_python; then
    install_python_tk
    if ! find_python; then
        echo "No Python 3.10+ with Tkinter found. Install Python 3.10+ and Tkinter (e.g. \"sudo apt install python3 python3-tk\") to use the GUI hub."
        exit 1
    fi
fi

exec "$PYTHON_BIN" "$SCRIPT_DIR/o3de_hub_gui.py" "$@"
