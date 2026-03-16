#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-ci}"
GENERATOR="${CMAKE_GENERATOR:-Ninja}"
BUILD_TYPE="${CMAKE_BUILD_TYPE:-RelWithDebInfo}"

# WP01 gate requires these core boundary tests to be registered under contract label.
DEFAULT_REQUIRED_TESTS=(
  "ContextPacketBoundaryContractTest"
  "RecoveryBoundaryContractTest"
  "MultiAgentBoundaryContractTest"
)

log() {
  printf '[WP01-GATE] %s\n' "$1"
}

resolve_required_tests() {
  if [[ -n "${WP01_GATE_REQUIRED_TESTS:-}" ]]; then
    IFS=',' read -r -a REQUIRED_TESTS <<< "${WP01_GATE_REQUIRED_TESTS}"
  else
    REQUIRED_TESTS=("${DEFAULT_REQUIRED_TESTS[@]}")
  fi
}

check_contract_test_registration() {
  local listing missing=0

  # ctest -N validates test registration without executing tests.
  listing="$(ctest --test-dir "${BUILD_DIR}" -N -L contract)"

  if ! grep -q 'Total Tests:' <<< "${listing}"; then
    log "failed to parse contract test listing from ctest -N"
    return 1
  fi

  for test_name in "${REQUIRED_TESTS[@]}"; do
    if ! grep -q "${test_name}" <<< "${listing}"; then
      log "missing required contract test registration: ${test_name}"
      missing=1
    fi
  done

  if [[ ${missing} -ne 0 ]]; then
    log "registration check failed"
    return 1
  fi

  log "registration check passed"
}

resolve_required_tests

log "configure: ${BUILD_DIR}"
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
  -G "${GENERATOR}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

log "build target: dasall_contract_tests"
cmake --build "${BUILD_DIR}" --target dasall_contract_tests

log "validate registration: ctest -N -L contract"
check_contract_test_registration

log "execute contract label tests"
ctest --test-dir "${BUILD_DIR}" -L contract --output-on-failure

log "WP01 contract gate passed"