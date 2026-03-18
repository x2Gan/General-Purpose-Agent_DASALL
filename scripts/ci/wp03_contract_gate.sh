#!/usr/bin/env bash
# ==========================================================================
# wp03_contract_gate.sh
#
# WP03-T018-B: CI gate script for the WP-03 main-flow contract freeze.
# Validates that all required WP-03 contract tests are registered and pass,
# providing a single-entry-point gate for CI pipelines and local verification.
#
# Design basis:
#   - WP03-T018-D M3冻结包 §9 Design→Build mapping
#   - wp01_contract_gate.sh / wp02_contract_gate.sh (pattern reference)
#   - M3ChecklistGuards.h 10 Gate (G9: contract tests 100% passed)
#
# Usage:
#   bash scripts/ci/wp03_contract_gate.sh
#
# Environment overrides:
#   BUILD_DIR             — build directory (default: <root>/build-ci)
#   CMAKE_GENERATOR       — generator (default: Ninja)
#   CMAKE_BUILD_TYPE      — build type (default: RelWithDebInfo)
#   WP03_GATE_REQUIRED_TESTS — comma-separated test list override
#
# Exit codes:
#   0 — WP-03 gate passed (all required + full contract suite pass)
#   1 — gate failed (missing registration or test failure)
# ==========================================================================
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-ci}"
GENERATOR="${CMAKE_GENERATOR:-Ninja}"
BUILD_TYPE="${CMAKE_BUILD_TYPE:-RelWithDebInfo}"

# ---------------------------------------------------------------------------
# WP-03 required tests: one representative test per major object/milestone.
# These are the minimum set that must be discoverable and passing for the
# M3 gate to be considered valid.
# ---------------------------------------------------------------------------
DEFAULT_REQUIRED_TESTS=(
  # T001-B: main flow aggregation smoke
  "MainFlowContractsSmokeTest"
  # T002-B: AgentRequest boundary
  "AgentRequestContractTest"
  # T003-B: AgentRequest field rules
  "AgentRequestFieldContractTest"
  # T004-B: GoalContract boundary
  "GoalContractContractTest"
  # T005-B: GoalContract field rules
  "GoalContractFieldContractTest"
  # T006-B: Observation boundary
  "ObservationContractTest"
  # T007-B: ObservationSource classification
  "ObservationSourceContractTest"
  # T008-B: ObservationDigest layering
  "ObservationDigestBoundaryContractTest"
  # T009-B: BeliefState prohibition
  "BeliefStateContractTest"
  # T010-B: ContextPacket main flow
  "ContextPacketMainFlowContractTest"
  # T011-B: ContextPacket field rules
  "ContextPacketFieldContractTest"
  # T012-B: Checkpoint recovery boundary
  "CheckpointContractTest"
  # T013-B: Checkpoint field rules
  "CheckpointFieldContractTest"
  # T014-B: AgentResult output boundary
  "AgentResultContractTest"
  # T015-B: end-to-end flow
  "MainFlowContractE2ETest"
  # T016-B: overlap guards
  "MainFlowOverlapContractTest"
  # T017-B: M3 checklist
  "M3ChecklistContractTest"
)

# ---------------------------------------------------------------------------
# Logging helper
# ---------------------------------------------------------------------------
log() {
  printf '[WP03-GATE] %s\n' "$1"
}

# ---------------------------------------------------------------------------
# Resolve required tests (allow override via environment variable)
# ---------------------------------------------------------------------------
resolve_required_tests() {
  if [[ -n "${WP03_GATE_REQUIRED_TESTS:-}" ]]; then
    IFS=',' read -r -a REQUIRED_TESTS <<< "${WP03_GATE_REQUIRED_TESTS}"
  else
    REQUIRED_TESTS=("${DEFAULT_REQUIRED_TESTS[@]}")
  fi
}

# ---------------------------------------------------------------------------
# Check that every required test is discoverable via ctest -N
# ---------------------------------------------------------------------------
check_contract_test_registration() {
  local listing missing=0

  # ctest -N lists tests without executing them.
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

# ---------------------------------------------------------------------------
# Run required tests first, then the full contract label suite
# ---------------------------------------------------------------------------
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

  log "execute required WP-03 contract tests: ${#tests[@]} targets"
  ctest --test-dir "${BUILD_DIR}" -R "${combined_pattern}" --output-on-failure

  # Run the full contract label suite to ensure no regressions across
  # WP-01, WP-02, and WP-03 contract test sets.
  log "execute full contract label suite"
  ctest --test-dir "${BUILD_DIR}" -L contract --output-on-failure
}

# ===========================================================================
# Main execution
# ===========================================================================
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

log "WP03 contract gate passed"
