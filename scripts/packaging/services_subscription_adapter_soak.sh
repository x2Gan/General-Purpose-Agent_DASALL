#!/bin/sh
set -eu

ARTIFACT_DIR=${DASALL_CAPABILITY_SERVICES_SOAK_ARTIFACT_DIR:-/tmp/dasall-capability-services-soak}
BUILD_DIR=${DASALL_BUILD_DIR:-build/vscode-linux-ninja}
ITERATIONS=${DASALL_CAPABILITY_SERVICES_SOAK_ITERATIONS:-10}
TEST_BINARY_REL=tests/integration/services/dasall_services_failure_integration_test

log() {
  printf '[services-soak] %s\n' "$*"
}

fail() {
  printf '[services-soak] %s\n' "$*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Usage: bash scripts/packaging/services_subscription_adapter_soak.sh [--artifact-dir <path>] [--build-dir <path>] [--iterations <count>]
EOF
}

ensure_positive_integer() {
  value=$1
  label=$2
  case "$value" in
    ''|*[!0-9]*) fail "${label}: expected a positive integer, got ${value}" ;;
  esac
  [ "$value" -ge 1 ] || fail "${label}: expected at least 1, got ${value}"
}

ensure_artifact_dir() {
  mkdir -p "$ARTIFACT_DIR"
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --artifact-dir)
      [ "$#" -ge 2 ] || fail 'missing value for --artifact-dir'
      ARTIFACT_DIR=$2
      shift 2
      ;;
    --build-dir)
      [ "$#" -ge 2 ] || fail 'missing value for --build-dir'
      BUILD_DIR=$2
      shift 2
      ;;
    --iterations)
      [ "$#" -ge 2 ] || fail 'missing value for --iterations'
      ITERATIONS=$2
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      usage
      fail "unknown option: $1"
      ;;
  esac
done

ensure_positive_integer "$ITERATIONS" 'iterations'
ensure_artifact_dir

TEST_BINARY_PATH=${BUILD_DIR}/${TEST_BINARY_REL}
[ -x "$TEST_BINARY_PATH" ] || fail "missing services failure integration binary: $TEST_BINARY_PATH"

iteration=1
while [ "$iteration" -le "$ITERATIONS" ]; do
  label=$(printf '%02d' "$iteration")
  log "running capability services soak iteration ${iteration}/${ITERATIONS}"
  if "$TEST_BINARY_PATH" > "$ARTIFACT_DIR/iteration-${label}.log" 2>&1; then
    :
  else
    fail "iteration ${label} failed; see $ARTIFACT_DIR/iteration-${label}.log"
  fi
  iteration=$((iteration + 1))
done

python3 - "$TEST_BINARY_PATH" "$ARTIFACT_DIR/services-soak-summary.json" "$ITERATIONS" <<'PY'
import json
import pathlib
import sys

binary_path = pathlib.Path(sys.argv[1])
output_path = pathlib.Path(sys.argv[2])
iterations = int(sys.argv[3])

summary = {
    'authoritative_test_binary': 'CapabilityServicesFailureIntegrationTest',
    'binary_path': str(binary_path),
    'iterations_requested': iterations,
    'iterations_completed': iterations,
    'all_passed': True,
    'covered_slices': ['remote_timeout', 'subscription_overflow'],
    'release_runner_local_artifact_ready': True,
}

output_path.write_text(json.dumps(summary, indent=2) + '\n', encoding='ascii')
PY

printf '%s\n' 'capability services soak passed' > "$ARTIFACT_DIR/status.txt"
log "wrote capability services soak artifacts to ${ARTIFACT_DIR}"