@ECHO OFF
REM Installs the AI Backbone ML libraries (requirements-ai.txt) into the engine's
REM Python environment. Run once (and again after editing requirements-ai.txt).
REM
REM Usage:
REM   install_ai_libs.bat          - CPU PyTorch (works everywhere)
REM   install_ai_libs.bat --cuda   - CUDA PyTorch (NVIDIA GPUs, cu128 wheels; supports RTX 50-series)

SETLOCAL
SET GEM_DIR=%~dp0
SET ENGINE_PIP=%GEM_DIR%..\..\python\pip.cmd

IF NOT EXIST "%ENGINE_PIP%" (
    ECHO [AIBackbone] Could not find the engine pip wrapper at %ENGINE_PIP%
    exit /B 1
)

IF "%~1"=="--cuda" (
    ECHO [AIBackbone] Installing ML libraries with CUDA PyTorch...
    CALL "%ENGINE_PIP%" install --index-url https://download.pytorch.org/whl/cu128 torch
    IF ERRORLEVEL 1 exit /B 1
)

ECHO [AIBackbone] Installing ML libraries from requirements-ai.txt...
CALL "%ENGINE_PIP%" install -r "%GEM_DIR%requirements-ai.txt"
IF ERRORLEVEL 1 exit /B 1

ECHO [AIBackbone] Done. Restart the Editor and open Tools -^> AI Model Builder.
exit /B 0
