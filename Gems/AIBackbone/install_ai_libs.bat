@ECHO OFF

REM 
REM Copyright (c) Contributors to the Open 3D Engine Project.
REM For complete copyright and license terms please see the LICENSE at the root of this distribution.
REM 
REM SPDX-License-Identifier: Apache-2.0 OR MIT
REM 

SETLOCAL
SET GEM_DIR=%~dp0
SET ENGINE_PYTHON=%GEM_DIR%..\..\python\python.cmd

IF NOT EXIST "%ENGINE_PYTHON%" (
    ECHO [AIBackbone] Could not find the engine Python wrapper at %ENGINE_PYTHON%
    exit /B 1
)

SET PYTHONPATH=%GEM_DIR%Editor\Scripts;%PYTHONPATH%
IF "%~1"=="--cuda" (
    SHIFT
    CALL "%ENGINE_PYTHON%" -c "import runpy,sys; sys.path.insert(0,sys.argv.pop(1)); sys.argv[0]='aibackbone.deps'; runpy.run_module('aibackbone.deps',run_name='__main__')" "%GEM_DIR%Editor\Scripts" --torch cu128 %*
) ELSE (
    CALL "%ENGINE_PYTHON%" -c "import runpy,sys; sys.path.insert(0,sys.argv.pop(1)); sys.argv[0]='aibackbone.deps'; runpy.run_module('aibackbone.deps',run_name='__main__')" "%GEM_DIR%Editor\Scripts" %*
)
EXIT /B %ERRORLEVEL%
