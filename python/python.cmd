@ECHO OFF
REM 
REM Copyright (c) Contributors to the Open 3D Engine Project.
REM For complete copyright and license terms please see the LICENSE at the root of this distribution.
REM
REM SPDX-License-Identifier: Apache-2.0 OR MIT
REM
REM

REM This script provides a single entry point that you can trust is present.
REM Depending on this entry point instead of trying to find a python.exe
REM In a subfolder allows you to keep working if the version of python changes or
REM other environmental requirements change.
REM When the project switches to a new version of Python, this file will be updated.

SETLOCAL
SET CMD_DIR=%~dp0
SET CMD_DIR=%CMD_DIR:~0,-1%

REM Calculate the path to the expected python venv for the current engine located at %CMD_DIR%\.. 
REM The logic in LYPython will generate a unique ID based on the absolute path of the current engine
REM so that the venv will not collide with any other versions of O3DE installed on the current machine


REM Run the custom cmake command script to calculate the ID based on %CMD_DIR%\.. 
SET CALC_PATH=%CMD_DIR%\..\cmake\CalculateEnginePathId.cmake
FOR /F %%g IN ('cmake -P "%CALC_PATH%" "%CMD_DIR%\.."') DO SET ENGINE_ID=%%g
IF NOT "%ENGINE_ID%" == "" GOTO ENGINE_ID_CALCULATED
echo
echo Unable to calculate engine ID
exit /b 1

:ENGINE_ID_CALCULATED

REM Set the expected location of the python venv for this engine and the locations of the critical scripts/executables 
REM needed to run python within the venv properly

REM If the %LY_3RDPARTY_PATH% is not set, then default it to %USERPROFILE%/.o3de/3rdParty
IF "" == "%LY_3RDPARTY_PATH%" (
    SET LY_3RDPARTY_PATH=%USERPROFILE%\.o3de\3rdParty
)

SET PYTHON_VENV=%USERPROFILE%\.o3de\Python\venv\%ENGINE_ID%
SET PYTHON_VENV_ACTIVATE=%PYTHON_VENV%\Scripts\activate.bat
SET PYTHON_VENV_DEACTIVATE=%PYTHON_VENV%\Scripts\deactivate.bat
IF [%1] EQU [debug] (
    SET PYTHON_VENV_PYTHON=%PYTHON_VENV%\Scripts\python_d.exe
    SET PYTHON_ARGS=%*:~6%
) ELSE (
    SET PYTHON_VENV_PYTHON=%PYTHON_VENV%\Scripts\python.exe
    SET PYTHON_ARGS=%*
)
:PYTHON_VENV_VALIDATE
IF NOT EXIST "%PYTHON_VENV_PYTHON%" GOTO PYTHON_SETUP_REQUIRED

REM If python venv exists, we still need to validate that it is the current version by getting the 
REM package current package hash from 3rd Party
FOR /F %%g IN ('cmake -P "%CMD_DIR%\get_python_package_hash.cmake" "%CMD_DIR%\.." Windows') DO SET CURRENT_PACKAGE_HASH=%%g
IF NOT "%CURRENT_PACKAGE_HASH%" == "" GOTO PACKAGE_HASH_READ
echo.
echo Unable to get current python package hash
exit /b 1


:PACKAGE_HASH_READ

REM Make sure there a .hash file that serves as the marker for the source python package the venv is from
SET PYTHON_VENV_HASH=%PYTHON_VENV%\.hash

IF NOT EXIST "%PYTHON_VENV_HASH%" GOTO PYTHON_SETUP_REQUIRED

REM Read in the .hash from the venv to see if we need to update the version of python
SET /p VENV_PACKAGE_HASH=<"%PYTHON_VENV_HASH%"

IF "%VENV_PACKAGE_HASH%" == "%CURRENT_PACKAGE_HASH%" GOTO PYTHON_VENV_MATCHES
GOTO PYTHON_SETUP_REQUIRED

:PYTHON_VENV_MATCHES
REM The hash only proves the venv was created from the right package; a failed/interrupted
REM dependency install can still leave it unusable, so verify the core deps actually import.
REM Skipped while get_python.bat is bootstrapping (pip itself runs through this script before
REM those dependencies exist).
IF DEFINED O3DE_PYTHON_BOOTSTRAPPING GOTO PYTHON_VENV_READY
call "%PYTHON_VENV_PYTHON%" -c "import packaging, resolvelib, o3de" >NUL 2>&1
IF ERRORLEVEL 1 GOTO PYTHON_SETUP_REQUIRED
:PYTHON_VENV_READY

REM Execute the python call from the arguments within the python venv environment

call "%PYTHON_VENV_ACTIVATE%"

call "%PYTHON_VENV_PYTHON%" -B %PYTHON_ARGS%
SET PYTHON_RESULT=%ERRORLEVEL%

call "%PYTHON_VENV_DEACTIVATE%"

exit /B %PYTHON_RESULT%

:PYTHON_SETUP_REQUIRED
REM O3DE uses its own pinned Python runtime (a downloaded 3rd Party package), not the system
REM Python, so when the per-engine venv above is missing or out of date it must be (re)created by
REM get_python.bat. Do that automatically here so first-time setup "just works". Guards:
REM   O3DE_AUTO_PYTHON_SETUP=0   -> opt out (e.g. CI that manages Python itself)
REM   O3DE_PYTHON_BOOTSTRAPPING  -> set while get_python.bat runs (which re-invokes this script),
REM                                 preventing infinite recursion.
REM   O3DE_PYTHON_SETUP_RETRIED  -> set after one auto-setup attempt so we do not loop forever.
IF "%O3DE_AUTO_PYTHON_SETUP%" == "0" GOTO PYTHON_SETUP_MANUAL
IF DEFINED O3DE_PYTHON_BOOTSTRAPPING GOTO PYTHON_SETUP_MANUAL
IF DEFINED O3DE_PYTHON_SETUP_RETRIED GOTO PYTHON_SETUP_FAILED

ECHO.
ECHO Setting up O3DE's Python environment automatically (this can take a few minutes the first time)...
ECHO   - To do this manually instead, run: %CMD_DIR%\get_python.bat
ECHO   - To disable automatic setup, set the environment variable O3DE_AUTO_PYTHON_SETUP=0
ECHO.

SET O3DE_PYTHON_BOOTSTRAPPING=1
CALL "%CMD_DIR%\get_python.bat"
SET GET_PYTHON_RESULT=%ERRORLEVEL%
SET O3DE_PYTHON_BOOTSTRAPPING=

IF NOT "%GET_PYTHON_RESULT%" == "0" (
    ECHO ERROR: Automatic Python setup failed ^(get_python.bat returned %GET_PYTHON_RESULT%^). See the log above.
    exit /b %GET_PYTHON_RESULT%
)

REM Re-validate now that Python should be set up.
SET O3DE_PYTHON_SETUP_RETRIED=1
GOTO PYTHON_VENV_VALIDATE

:PYTHON_SETUP_FAILED
ECHO ERROR: O3DE's Python environment is still not valid after running get_python.bat.
exit /b 1

:PYTHON_SETUP_MANUAL
ECHO Python has not been set up completely for O3DE.
ECHO Run "%CMD_DIR%\get_python.bat" to set up Python for O3DE.
exit /b 1
