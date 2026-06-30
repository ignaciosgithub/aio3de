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

where python >NUL 2>NUL
IF %ERRORLEVEL% EQU 0 (
    SET "PYTHON_BIN=python"
) ELSE (
    where py >NUL 2>NUL
    IF %ERRORLEVEL% EQU 0 SET "PYTHON_BIN=py"
)

IF "%PYTHON_BIN%"=="" (
    ECHO No system Python found on PATH. Install Python 3.10+ from python.org to use the GUI hub.
    EXIT /b 1
)

"%PYTHON_BIN%" "%SCRIPT_DIR%o3de_hub_gui.py" %*
EXIT /b %ERRORLEVEL%
