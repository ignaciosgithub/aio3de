@echo off

REM 
REM Copyright (c) Contributors to the Open 3D Engine Project.
REM For complete copyright and license terms please see the LICENSE at the root of this distribution.
REM 
REM SPDX-License-Identifier: Apache-2.0 OR MIT
REM 

setlocal EnableDelayedExpansion
::
:: aio3de feature test harness (Windows)
::
:: Runs the functionality unit suites and the performance benchmarks for the
:: engine features added on top of stock O3DE (SIMD frustum culling, ParallelFor,
:: the portable ray-tracing BVH, and ray-traced hard shadows), and writes a
:: timestamped report to feature_tests\results\.
::
:: Usage:
::   run_all.bat [BUILD_DIR] [CONFIG]
::     BUILD_DIR  path to the CMake build tree   (default: <project>\build\windows)
::     CONFIG     profile | debug | release      (default: profile)
::
::   Set AIO3DE_BUILD_DIR to override the build tree without passing an argument,
::   e.g. when building the engine directly rather than a project.
::

set "SCRIPT_DIR=%~dp0"
set "ENGINE_ROOT=%SCRIPT_DIR%.."

:: --- resolve build dir + config ---------------------------------------------
set "BUILD_DIR=%~1"
if "%BUILD_DIR%"=="" set "BUILD_DIR=%AIO3DE_BUILD_DIR%"
if "%BUILD_DIR%"=="" set "BUILD_DIR=C:\Users\isavi\aio3detest\build\windows"

set "CONFIG=%~2"
if "%CONFIG%"=="" set "CONFIG=profile"

set "BIN_DIR=%BUILD_DIR%\bin\%CONFIG%"
set "RUNNER=%BIN_DIR%\AzTestRunner.exe"
set "TESTLIB=%BIN_DIR%\AzCore.Tests.dll"

if not exist "%RUNNER%" (
    echo [ERROR] AzTestRunner.exe not found at "%RUNNER%".
    echo         Build it first:  cmake --build "%BUILD_DIR%" --target AzTestRunner AzCore.Tests --config %CONFIG% -- /m:2
    exit /b 1
)
if not exist "%TESTLIB%" (
    echo [ERROR] AzCore.Tests.dll not found at "%TESTLIB%".
    echo         Build it first:  cmake --build "%BUILD_DIR%" --target AzCore.Tests --config %CONFIG% -- /m:2
    exit /b 1
)

:: --- report file ------------------------------------------------------------
set "RESULTS_DIR=%SCRIPT_DIR%results"
if not exist "%RESULTS_DIR%" mkdir "%RESULTS_DIR%"
for /f "tokens=1-6 delims=/:. " %%a in ("%date% %time%") do set "STAMP=%%c%%b%%a_%%d%%e%%f"
set "STAMP=%STAMP: =0%"
set "REPORT=%RESULTS_DIR%\report_%STAMP%.txt"

echo aio3de feature test report                                 > "%REPORT%"
echo build dir : %BUILD_DIR%                                    >> "%REPORT%"
echo config    : %CONFIG%                                       >> "%REPORT%"
echo timestamp : %date% %time%                                  >> "%REPORT%"
echo ============================================================>> "%REPORT%"

set "FAILED=0"

:: --- functionality suites ---------------------------------------------------
call :run_functional "SIMD frustum culling (#4)"   "MATH_FrustumCull.*:FrustumCullParallelFixture.*"
call :run_functional "ParallelFor primitive (#2)"  "ParallelForTestFixture.*:ParallelForEachChunk*"
call :run_functional "Ray-tracing BVH core (#8)"   "MATH_RayTracingBvh.*"
call :run_functional "Ray-traced hard shadows (#14)" "MATH_RayTracedShadows.*"
call :run_functional "XPBD soft body physics" "SoftBodyTests.*"

:: --- performance benchmarks -------------------------------------------------
call :run_bench "CPU feature benchmarks" "BM_FrustumCull|ParallelForBenchmarkFixture|BM_RayTracingBvh|BM_SoftBodyStep"

echo ============================================================>> "%REPORT%"
if "%FAILED%"=="0" (
    echo RESULT: all functionality suites PASSED                >> "%REPORT%"
) else (
    echo RESULT: %FAILED% functionality suites FAILED            >> "%REPORT%"
)

type "%REPORT%"
echo.
echo Report written to %REPORT%
echo.
echo For the GPU-driven features (GPU culling #7/#9, ray-traced shadows pass #14)
echo see feature_tests\README.md - those are exercised in the Editor; this harness
echo verifies their CPU reference + shader/pass registration.

if "%FAILED%"=="0" ( exit /b 0 ) else ( exit /b 1 )

:: ---------------------------------------------------------------------------
:: NOTE: suite names contain parentheses, and %-expansion happens before cmd
:: parses block parens, so these subroutines avoid parenthesized if/else blocks
:: entirely (goto instead) and only echo names via delayed expansion.
:run_functional
set "NAME=%~1"
set "FILTER=%~2"
echo.>> "%REPORT%"
echo [FUNCTIONALITY] !NAME!                                     >> "%REPORT%"
echo   filter: !FILTER!                                          >> "%REPORT%"
echo Running functionality: !NAME!
"%RUNNER%" "%TESTLIB%" AzRunUnitTests "--gtest_filter=%~2" >> "%REPORT%" 2>&1
if errorlevel 1 goto :functional_failed
echo   -^> PASSED                                               >> "%REPORT%"
echo   -^> PASSED: !NAME!
exit /b 0
:functional_failed
echo   -^> FAILED                                               >> "%REPORT%"
echo   -^> FAILED: !NAME!
set /a FAILED+=1
exit /b 0

:run_bench
set "NAME=%~1"
set "FILTER=%~2"
echo.>> "%REPORT%"
echo [BENCHMARK] !NAME!                                         >> "%REPORT%"
echo   filter: !FILTER!                                          >> "%REPORT%"
echo Running benchmarks: !NAME! (this takes a minute)...
"%RUNNER%" "%TESTLIB%" AzRunBenchmarks "--benchmark_filter=%~2" --benchmark_min_time=0.1 >> "%REPORT%" 2>&1
echo   -^> benchmark output captured                            >> "%REPORT%"
exit /b 0
