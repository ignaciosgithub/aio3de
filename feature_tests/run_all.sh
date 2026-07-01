#!/usr/bin/env bash
#
# aio3de feature test harness (Linux/macOS)
#
# Runs the functionality unit suites and the performance benchmarks for the
# engine features added on top of stock O3DE (SIMD frustum culling, ParallelFor,
# the portable ray-tracing BVH, and ray-traced hard shadows), and writes a
# timestamped report to feature_tests/results/.
#
# Usage:
#   run_all.sh [BUILD_DIR] [CONFIG]
#     BUILD_DIR  path to the CMake build tree   (default: <engine>/build/linux)
#     CONFIG     profile | debug | release      (default: profile)
#
#   Set AIO3DE_BUILD_DIR to override the build tree without passing an argument.
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENGINE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_DIR="${1:-${AIO3DE_BUILD_DIR:-${ENGINE_ROOT}/build/linux}}"
CONFIG="${2:-profile}"

BIN_DIR="${BUILD_DIR}/bin/${CONFIG}"
RUNNER="${BIN_DIR}/AzTestRunner"
TESTLIB="${BIN_DIR}/libAzCore.Tests.so"
[[ "$(uname)" == "Darwin" ]] && TESTLIB="${BIN_DIR}/libAzCore.Tests.dylib"

if [[ ! -x "${RUNNER}" ]]; then
    echo "[ERROR] AzTestRunner not found at ${RUNNER}"
    echo "        Build it first:  cmake --build \"${BUILD_DIR}\" --target AzTestRunner AzCore.Tests --config ${CONFIG}"
    exit 1
fi
if [[ ! -f "${TESTLIB}" ]]; then
    echo "[ERROR] AzCore.Tests library not found at ${TESTLIB}"
    echo "        Build it first:  cmake --build \"${BUILD_DIR}\" --target AzCore.Tests --config ${CONFIG}"
    exit 1
fi

RESULTS_DIR="${SCRIPT_DIR}/results"
mkdir -p "${RESULTS_DIR}"
STAMP="$(date +%Y%m%d_%H%M%S)"
REPORT="${RESULTS_DIR}/report_${STAMP}.txt"

{
    echo "aio3de feature test report"
    echo "build dir : ${BUILD_DIR}"
    echo "config    : ${CONFIG}"
    echo "timestamp : $(date)"
    echo "============================================================"
} > "${REPORT}"

FAILED=0

run_functional() {
    local name="$1" filter="$2"
    {
        echo
        echo "[FUNCTIONALITY] ${name}"
        echo "  filter: ${filter}"
    } >> "${REPORT}"
    echo "Running functionality: ${name}"
    if "${RUNNER}" "${TESTLIB}" AzRunUnitTests --gtest_filter="${filter}" >> "${REPORT}" 2>&1; then
        echo "  -> PASSED" >> "${REPORT}"
        echo "  -> PASSED: ${name}"
    else
        echo "  -> FAILED" >> "${REPORT}"
        echo "  -> FAILED: ${name}"
        FAILED=$((FAILED + 1))
    fi
}

run_bench() {
    local name="$1" filter="$2"
    {
        echo
        echo "[BENCHMARK] ${name}"
        echo "  filter: ${filter}"
    } >> "${REPORT}"
    echo "Running benchmarks: ${name} (this takes a minute)..."
    # AzTestRunner benchmark mode returns 0 then aborts during static teardown;
    # the results are already printed, so ignore the teardown exit status.
    "${RUNNER}" "${TESTLIB}" AzRunBenchmarks --benchmark_filter="${filter}" --benchmark_min_time=0.1 >> "${REPORT}" 2>&1 || true
    echo "  -> benchmark output captured" >> "${REPORT}"
}

run_functional "SIMD frustum culling (#4)"     "MATH_FrustumCull.*:FrustumCullParallelFixture.*"
run_functional "ParallelFor primitive (#2)"    "ParallelForTestFixture.*:ParallelForEachChunk*"
run_functional "Ray-tracing BVH core (#8)"     "MATH_RayTracingBvh.*"
run_functional "Ray-traced hard shadows (#14)" "MATH_RayTracedShadows.*"

run_bench "CPU feature benchmarks" "BM_FrustumCull|ParallelForBenchmarkFixture|BM_RayTracingBvh"

{
    echo "============================================================"
    if [[ "${FAILED}" -eq 0 ]]; then
        echo "RESULT: all functionality suites PASSED"
    else
        echo "RESULT: ${FAILED} functionality suite(s) FAILED"
    fi
} >> "${REPORT}"

cat "${REPORT}"
echo
echo "Report written to ${REPORT}"

exit "${FAILED}"
