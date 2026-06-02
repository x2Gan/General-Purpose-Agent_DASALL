#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/tsan}"
SUPPRESSIONS_FILE="${ROOT_DIR}/scripts/ci/memory_tsan.supp"

cd "${ROOT_DIR}"

rm -rf "${BUILD_DIR}"

if [[ -n "${TSAN_OPTIONS:-}" ]]; then
  export TSAN_OPTIONS="suppressions=${SUPPRESSIONS_FILE}:${TSAN_OPTIONS}"
else
  export TSAN_OPTIONS="suppressions=${SUPPRESSIONS_FILE}"
fi

cmake --preset tsan
cmake --build --preset tsan --target \
  dasall_memory_concurrency_stress_unit_test \
  dasall_memory_long_running_soak_integration_test

ctest --preset tsan -R \
  '^(MemoryConcurrencyStressTest|MemoryLongRunningSoakTest)$'