#!/usr/bin/env bash
# ==========================================================================
# wp05_contract_gate.sh
#
# WP05-T021-B: CI gate script for the WP-05 sub-domain + contract-tests
# governance freeze (M5/V1 Ready).
#
# Script responsibilities:
#   1) Configure/build the contract test target (unless explicitly skipped).
#   2) Verify that all WP05 required tests are discoverable via `ctest -N`.
#   3) Execute the WP05 required tests as a focused gate subset.
#   4) Execute the full `contract` label suite as the final release gate.
#
# Design basis:
#   - docs/todos/contracts-freeze/WP-05-子域细化与ContractTestsTODO.md
#   - docs/todos/contracts-freeze/deliverables/WP05-T020-评审纪要.md
#   - contracts/include/boundary/V1ReadyChecklistGuards.h (G10)
#   - scripts/ci/wp04_contract_gate.sh (single-entry gate pattern)
#
# Usage:
#   bash scripts/ci/wp05_contract_gate.sh
#
# Environment overrides:
#   BUILD_DIR                 — build directory (default: <root>/build-ci)
#   CMAKE_GENERATOR           — generator (default: Ninja)
#   CMAKE_BUILD_TYPE          — build type (default: RelWithDebInfo)
#   WP05_GATE_REQUIRED_TESTS  — comma-separated required-test override
#   WP05_GATE_SKIP_CONFIGURE  — set to 1 to skip cmake configure
#   WP05_GATE_SKIP_BUILD      — set to 1 to skip cmake build
#
# Exit codes:
#   0 — WP-05 gate passed
#   1 — gate failed (registration missing or test failure)
# ==========================================================================
set -euo pipefail

# Resolve workspace-relative paths and toolchain defaults once at startup so
# every downstream command uses the same deterministic context.
ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-ci}"
GENERATOR="${CMAKE_GENERATOR:-Ninja}"
BUILD_TYPE="${CMAKE_BUILD_TYPE:-RelWithDebInfo}"
SKIP_CONFIGURE="${WP05_GATE_SKIP_CONFIGURE:-0}"
SKIP_BUILD="${WP05_GATE_SKIP_BUILD:-0}"

# The default required-test set maps 1:1 to WP05-T001-B through WP05-T020-B.
# This keeps the gate aligned with the frozen Design->Build chain and blocks
# partial registration drift.
DEFAULT_REQUIRED_TESTS=(
  "DomainRolloutContractTest"
  "ToolRequestContractTest"
  "ToolResultContractTest"
  "ToolDescriptorIRContractTest"
  "PromptSpecReleaseContractTest"
  "TurnSessionSummaryMemoryContractTest"
  "MemoryFactExperienceContractTest"
  "TaskDomainContractTest"
  "EventTypePayloadContractTest"
  "LLMRequestResponseContractTest"
  "InterfaceCatalogContractTest"
  "InterfaceAdmissionContractTest"
  "SerializationCompatibilityContractTest"
  "ErrorCodeEnumCompatibilityContractTest"
  "EventEnvelopeCompatibilityContractTest"
  "ADRBoundaryRegressionContractTest"
  "CoverageMatrixContractTest"
  "VersionChangeSchemaContractTest"
  "BreakingReviewContractTest"
  "V1ReadyChecklistContractTest"
)

# Prefix every log line so CI outputs can be filtered and traced quickly.
log() {
  printf '[WP05-GATE] %s\n' "$1"
}

# Allow required-test override for diagnostics while preserving deterministic
# defaults in normal gate runs.
resolve_required_tests() {
  if [[ -n "${WP05_GATE_REQUIRED_TESTS:-}" ]]; then
    IFS=',' read -r -a REQUIRED_TESTS <<< "${WP05_GATE_REQUIRED_TESTS}"
  else
    REQUIRED_TESTS=("${DEFAULT_REQUIRED_TESTS[@]}")
  fi
}

# Validate that every required test is discoverable before execution.
# Registration checks fail fast on missing entries and prevent false-positive
# gate passes caused by stale build artifacts or forgotten CMake wiring.
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

# Execute required tests first (focused signal), then full contract suite
# (regression signal) to satisfy V1 Ready freeze gate semantics.
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

  log "execute required WP-05 contract tests: ${#tests[@]} targets"
  ctest --test-dir "${BUILD_DIR}" -R "${combined_pattern}" --output-on-failure

  log "execute full contract label suite"
  ctest --test-dir "${BUILD_DIR}" -L contract --output-on-failure
}

# Main pipeline: resolve inputs -> optional configure/build -> registration
# validation -> required+full test execution.
resolve_required_tests

if [[ "${SKIP_CONFIGURE}" != "1" ]]; then
  log "configure: ${BUILD_DIR}"
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -G "${GENERATOR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
else
  log "skip configure (WP05_GATE_SKIP_CONFIGURE=1)"
fi

if [[ "${SKIP_BUILD}" != "1" ]]; then
  log "build target: dasall_contract_tests"
  cmake --build "${BUILD_DIR}" --target dasall_contract_tests
else
  log "skip build (WP05_GATE_SKIP_BUILD=1)"
fi

log "validate registration: ctest -N -L contract"
check_contract_test_registration

run_contract_tests "${REQUIRED_TESTS[@]}"

log "WP05 contract gate passed"
