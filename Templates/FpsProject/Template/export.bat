@echo off
:: Copyright (c) Contributors to the Open 3D Engine Project.
:: For complete copyright and license terms please see the LICENSE at the root of this distribution.
:: SPDX-License-Identifier: Apache-2.0 OR MIT
setlocal
set "O3DE_PATH=${EnginePath}"
set "O3DE_PROJECT_PATH=%~dp0"
set "O3DE_PROJECT_SEEDLIST=%O3DE_PROJECT_PATH%\AssetBundling\SeedLists\Arena.seed"
set "TOOLS_BUILD_PATH=%O3DE_PROJECT_PATH%build\windows"
if "%~1"=="-h" (
    echo Usage: %~nx0 [EXPORT_PATH]
    exit /B 0
)
if "%~1"=="" (
    set "OUTPUT_PATH=%O3DE_PROJECT_PATH%build\export"
) else (
    set "OUTPUT_PATH=%~1"
)
if not exist "%O3DE_PATH%\scripts\o3de.bat" (
    echo Invalid engine path: %O3DE_PATH% 1>&2
    exit /B 1
)
call "%O3DE_PATH%\scripts\o3de.bat" export-project -es ExportScripts\export_source_built_project.py --project-path "%O3DE_PROJECT_PATH%" --log-level INFO --tools-build-path "%TOOLS_BUILD_PATH%" --seedlist "%O3DE_PROJECT_SEEDLIST%" -out "%OUTPUT_PATH%"
