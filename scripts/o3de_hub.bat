@ECHO OFF
REM
REM Copyright (c) Contributors to the Open 3D Engine Project.
REM For complete copyright and license terms please see the LICENSE at the root of this distribution.
REM
REM SPDX-License-Identifier: Apache-2.0 OR MIT
REM
REM

REM Launches the cross-platform Tkinter GUI hub. This deliberately uses the SYSTEM Python (not the
REM engine's bundled venv) so it works on a fresh clone before the bundled Python has been set up -
REM the whole point of the hub is to help you get to that point.

SETLOCAL

SET "SCRIPT_DIR=%~dp0"
SET "PYTHON_BIN="
SET "PYTHON_ARGS="
SET "PY_CHECK=import sys, tkinter; sys.exit(0 if sys.version_info >= (3, 10) else 1)"
SET "USER_PYTHON=%LOCALAPPDATA%\Programs\Python\Python312\python.exe"

REM "where python" also matches the Microsoft Store alias stub (WindowsApps\python.exe), which only
REM opens the Store, so actually run each candidate and require Python 3.10+ with Tkinter.
python -c "%PY_CHECK%" >NUL 2>NUL
IF NOT ERRORLEVEL 1 (
    SET "PYTHON_BIN=python"
    GOTO :launch
)
py -3 -c "%PY_CHECK%" >NUL 2>NUL
IF NOT ERRORLEVEL 1 (
    SET "PYTHON_BIN=py"
    SET "PYTHON_ARGS=-3"
    GOTO :launch
)
IF EXIST "%USER_PYTHON%" GOTO :use_user_python

where winget >NUL 2>NUL
IF ERRORLEVEL 1 GOTO :no_python
ECHO No usable Python 3.10+ found. Installing Python 3.12 for this user with winget...
winget install --id Python.Python.3.12 -e --scope user --accept-source-agreements --accept-package-agreements
IF EXIST "%USER_PYTHON%" GOTO :use_user_python
GOTO :no_python

:use_user_python
SET "PYTHON_BIN=%USER_PYTHON%"
GOTO :launch

:no_python
ECHO No system Python 3.10+ found. Install Python 3.10+ from python.org to use the GUI hub.
EXIT /b 1

:launch
"%PYTHON_BIN%" %PYTHON_ARGS% "%SCRIPT_DIR%o3de_hub_gui.py" %*
EXIT /b %ERRORLEVEL%
