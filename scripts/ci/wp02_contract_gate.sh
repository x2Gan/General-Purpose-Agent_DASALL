#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-ci}"
GENERATOR="${CMAKE_GENERATOR:-Ninja}"
BUILD_TYPE="${CMAKE_BUILD_TYPE:-RelWithDebInfo}"

# WP02 gate requires core cross-cutting contract tests to be registered.
# These tests anchor M2 gate evidence for timeout migration, enum downgrade,
# event header whitelist, and checklist automation.
DEFAULT_REQUIRED_TESTS=(
  "CompatibilityContractTest"
  "TimeDeadlineContractTest"
  "EventEnvelopeContractTest"
  "EnumLifecycleContractTest"
  "M2ChecklistContractTest"
)

log() {
  printf '[WP02-GATE] %s\n' "$1"
}

resolve_required_tests() {
  if [[ -n "${WP02_GATE_REQUIRED_TESTS:-}" ]]; then
    IFS=',' read -r -a REQUIRED_TESTS <<< "${WP02_GATE_REQUIRED_TESTS}"
  else
    REQUIRED_TESTS=("${DEFAULT_REQUIRED_TESTS[@]}")
  fi
}

check_contract_test_registration() {
  local listing missing=0

  # ctest -N checks whether expected contract tests are discoverable.
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

run_contract_tests() {
  local tests=("$@")
  local combined_pattern
  local test_name

  combined_pattern=""
  for test_name in "${tests[@]}"; do
    if [[ -n "${combined_pattern}" ]]; then
      combined_pattern+="|"
    fi
    combined_pattern+="${test_name}"
  done

  log "execute required contract tests: ${combined_pattern}"
  ctest --test-dir "${BUILD_DIR}" -R "${combined_pattern}" --output-on-failure

  # Print grouped summary to keep CI output aligned with B012 orchestration.
  log "execute full contract label suite"
  ctest --test-dir "${BUILD_DIR}" -L contract --output-on-failure
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

run_contract_tests "${REQUIRED_TESTS[@]}"

log "WP02 contract gate passed"
