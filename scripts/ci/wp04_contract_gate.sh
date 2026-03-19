#!/usr/bin/env bash
# ==========================================================================
# wp04_contract_gate.sh
#
# WP04-T024-B: CI gate script for the WP-04 boundary-object freeze.
# Validates that all required WP-04 contract tests are registered and passing,
# then runs the full contract suite as the release gate for the M4 freeze.
#
# Design basis:
#   - WP04-T024-D M4冻结包 §6/§7/§9
#   - WP04-T023-D M4 checklist (G9 requires contract-suite green)
#   - wp01/wp02/wp03 contract gate scripts (pattern reference)
#
# Usage:
#   bash scripts/ci/wp04_contract_gate.sh
#
# Environment overrides:
#   BUILD_DIR                 — build directory (default: <root>/build-ci)
#   CMAKE_GENERATOR           — generator (default: Ninja)
#   CMAKE_BUILD_TYPE          — build type (default: RelWithDebInfo)
#   WP04_GATE_REQUIRED_TESTS  — comma-separated required test override
#   WP04_GATE_SKIP_CONFIGURE  — when set to 1, skip cmake configure
#   WP04_GATE_SKIP_BUILD      — when set to 1, skip cmake build
#
# Exit codes:
#   0 — WP-04 gate passed
#   1 — gate failed (missing registration or test failure)
# ==========================================================================
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-ci}"
GENERATOR="${CMAKE_GENERATOR:-Ninja}"
BUILD_TYPE="${CMAKE_BUILD_TYPE:-RelWithDebInfo}"
SKIP_CONFIGURE="${WP04_GATE_SKIP_CONFIGURE:-0}"
SKIP_BUILD="${WP04_GATE_SKIP_BUILD:-0}"

DEFAULT_REQUIRED_TESTS=(
  "PromptBoundaryContractsSmokeTest"
  "PromptComposeRequestContractTest"
  "PromptComposeRequestFieldContractTest"
  "PromptComposeResultContractTest"
  "PromptComposeResultFieldContractTest"
  "RecoveryBoundaryContractsSmokeTest"
  "ReflectionDecisionContractTest"
  "ReflectionDecisionFieldContractTest"
  "RecoveryRequestContractTest"
  "RecoveryRequestFieldContractTest"
  "RecoveryOutcomeContractTest"
  "RecoveryOutcomeFieldContractTest"
  "MultiAgentBoundaryContractsSmokeTest"
  "MultiAgentRequestContractTest"
  "MultiAgentRequestFieldContractTest"
  "MultiAgentResultContractTest"
  "MultiAgentResultFieldContractTest"
  "WorkerTaskContractTest"
  "WorkerTaskFieldContractTest"
  "WorkerLeaseContractTest"
  "WorkerLeaseFieldContractTest"
  "ADRFieldMappingContractTest"
  "M4ChecklistContractTest"
)

log() {
  printf '[WP04-GATE] %s\n' "$1"
}

resolve_required_tests() {
  if [[ -n "${WP04_GATE_REQUIRED_TESTS:-}" ]]; then
    IFS=',' read -r -a REQUIRED_TESTS <<< "${WP04_GATE_REQUIRED_TESTS}"
  else
    REQUIRED_TESTS=("${DEFAULT_REQUIRED_TESTS[@]}")
  fi
}

check_contract_test_registration() {
  local listing missing=0

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

  log "registration check passed (${#REQUIRED_TESTS[@]} required tests found)"
}

run_contract_tests() {
  local tests=("$@")
  local combined_pattern=""
  local test_name

  for test_name in "${tests[@]}"; do
    if [[ -n "${combined_pattern}" ]]; then
      combined_pattern+="|"
    fi
    combined_pattern+="${test_name}"
  done

  log "execute required WP-04 contract tests: ${#tests[@]} targets"
  ctest --test-dir "${BUILD_DIR}" -R "${combined_pattern}" --output-on-failure

  log "execute full contract label suite"
  ctest --test-dir "${BUILD_DIR}" -L contract --output-on-failure
}

resolve_required_tests

if [[ "${SKIP_CONFIGURE}" != "1" ]]; then
  log "configure: ${BUILD_DIR}"
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -G "${GENERATOR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
else
  log "skip configure (WP04_GATE_SKIP_CONFIGURE=1)"
fi

if [[ "${SKIP_BUILD}" != "1" ]]; then
  log "build target: dasall_contract_tests"
  cmake --build "${BUILD_DIR}" --target dasall_contract_tests
else
  log "skip build (WP04_GATE_SKIP_BUILD=1)"
fi

log "validate registration: ctest -N -L contract"
check_contract_test_registration

run_contract_tests "${REQUIRED_TESTS[@]}"

log "WP04 contract gate passed"