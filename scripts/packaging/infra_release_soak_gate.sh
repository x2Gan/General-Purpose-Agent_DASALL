#!/bin/sh
set -eu

ARTIFACT_DIR=${DASALL_INFRA_RELEASE_SOAK_ARTIFACT_DIR:-/tmp/dasall-infra-release-soak}
BUILD_DIR=${DASALL_BUILD_DIR:-build/vscode-linux-ninja}
ITERATIONS=${DASALL_INFRA_RELEASE_SOAK_ITERATIONS:-10}
TIMEOUT_MS=${DASALL_INFRA_RELEASE_SOAK_TIMEOUT_MS:-30000}
PACKAGE_SMOKE_ARTIFACT_DIR=${DASALL_PACKAGE_SMOKE_ARTIFACT_DIR:-}
LLM_SECRET_PATH=/var/lib/dasall/secrets/llm/providers/deepseek-prod.secret

TEMP_DIR=
PROFILE_ID=
SECRET_ALREADY_CONFIGURED=0

log() {
  printf '[infra-soak] %s\n' "$*"
}

fail() {
  printf '[infra-soak] %s\n' "$*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Usage: bash scripts/packaging/infra_release_soak_gate.sh [--artifact-dir <path>] [--build-dir <path>] [--iterations <count>] [--timeout-ms <value>]
EOF
}

cleanup() {
  if [ -n "${TEMP_DIR}" ] && [ -d "${TEMP_DIR}" ]; then
    rm -rf "${TEMP_DIR}"
  fi
}

trap cleanup EXIT

require_command() {
  command -v "$1" >/dev/null 2>&1 || fail "missing required command: $1"
}

require_root_access() {
  if [ "$(id -u)" -eq 0 ]; then
    return 0
  fi

  command -v sudo >/dev/null 2>&1 || fail 'root or sudo is required'
  sudo -n true >/dev/null 2>&1 || fail 'passwordless sudo is required for infra release/soak gate; rerun as root'
}

run_root() {
  require_root_access
  if [ "$(id -u)" -eq 0 ]; then
    "$@"
  else
    sudo "$@"
  fi
}

run_root_sh() {
  require_root_access
  if [ "$(id -u)" -eq 0 ]; then
    sh -eu -c "$1"
  else
    sudo sh -eu -c "$1"
  fi
}

ensure_positive_integer() {
  integer_value=$1
  integer_label=$2
  case "$integer_value" in
    ''|*[!0-9]*) fail "${integer_label}: expected a positive integer, got ${integer_value}" ;;
  esac
  [ "$integer_value" -ge 1 ] || fail "${integer_label}: expected at least 1, got ${integer_value}"
}

ensure_artifact_dir() {
  mkdir -p "$ARTIFACT_DIR"
}

write_artifact_file() {
  file_name=$1
  file_content=$2

  ensure_artifact_dir
  printf '%s\n' "$file_content" > "$ARTIFACT_DIR/$file_name"
}

assert_json_contains() {
  json_payload=$1
  expected=$2
  assert_label=$3

  printf '%s\n' "$json_payload" | grep -Fq "$expected" || \
    fail "${assert_label}: expected JSON fragment not found: ${expected}"
}

readiness_is_ready() {
  json_payload=$1

  printf '%s\n' "$json_payload" | grep -Fq '"disposition":"completed"' || return 1
  printf '%s\n' "$json_payload" | grep -Fq '\"state\":\"READY\"'
}

assert_readiness_ready() {
  json_payload=$1
  readiness_label=$2

  readiness_is_ready "$json_payload" || fail "${readiness_label}: readiness payload did not reach READY: ${json_payload}"
}

resolve_profile_id() {
  run_root_sh '
    . /etc/default/dasall-daemon
    : "${DASALL_DAEMON_PROFILE_ID:?missing DASALL_DAEMON_PROFILE_ID}"
    printf "%s\n" "${DASALL_DAEMON_PROFILE_ID}"
  '
}

prepare_desired_state() {
  TEMP_DIR=$(mktemp -d /tmp/dasall-infra-release-soak.XXXXXX)
  PROFILE_ID=$(resolve_profile_id)
  DESIRED_FILE=${TEMP_DIR}/desired.yaml
  IMPORT_FILE=${TEMP_DIR}/deepseek-prod.key

  if [ -f "$LLM_SECRET_PATH" ]; then
    SECRET_ALREADY_CONFIGURED=1
  else
    : "${DASALL_DEEPSEEK_API_KEY_FILE:?missing DASALL_DEEPSEEK_API_KEY_FILE or ${LLM_SECRET_PATH}}"
    [ -f "${DASALL_DEEPSEEK_API_KEY_FILE}" ] || fail 'DASALL_DEEPSEEK_API_KEY_FILE does not point to a readable file'
    cp "${DASALL_DEEPSEEK_API_KEY_FILE}" "$IMPORT_FILE"
    chmod 600 "$IMPORT_FILE"
  fi

  cat <<EOF > "$DESIRED_FILE"
schema_version: dasall.config.apply.v1
profile_id: ${PROFILE_ID}
daemon:
  socket_path: /run/dasall/daemon.sock
  log_format: text
  diag_enabled: true
  override_enabled: false
  watchdog_enabled: false
service:
  start_now: true
  enable_on_boot: true
operator_access:
  add_users: []
EOF

  if [ "$SECRET_ALREADY_CONFIGURED" -eq 0 ]; then
    cat <<EOF >> "$DESIRED_FILE"
secrets:
  refs:
    - ref: secret://llm/providers/deepseek-prod
      source: file:${IMPORT_FILE}
      auth_profile_name: primary
EOF
  fi

  chmod 600 "$DESIRED_FILE"
}

apply_diag_enabled_config() {
  APPLY_JSON=$(run_root dasall-cli config apply --from-file "$DESIRED_FILE" --no-input --json)
  assert_json_contains "$APPLY_JSON" '"outcome":"applied"' 'infra diag enable config apply'
  if [ "$SECRET_ALREADY_CONFIGURED" -eq 0 ]; then
    assert_json_contains "$APPLY_JSON" '"written_secret_refs":["secret://llm/providers/deepseek-prod"]' 'infra diag enable config apply secret import'
  fi
  write_artifact_file 'config-apply.json' "$APPLY_JSON"
}

wait_for_daemon_ready() {
  attempt=0
  while [ "$attempt" -lt 90 ]; do
    if run_root_sh 'systemctl is-enabled --quiet dasall-daemon.service >/dev/null 2>&1 &&
       systemctl is-active --quiet dasall-daemon.service >/dev/null 2>&1'; then
      if readiness_json=$(run_root dasall-cli readiness --json --timeout-ms "$TIMEOUT_MS" 2>/dev/null); then
        if readiness_is_ready "$readiness_json"; then
          printf '%s\n' "$readiness_json"
          return 0
        fi
      fi
    fi

    attempt=$((attempt + 1))
    sleep 1
  done

  run_root systemctl status --no-pager dasall-daemon.service >&2 || true
  fail 'timed out waiting for daemon ready state'
}

run_installed_iteration() {
  label=$1

  readiness_json=$(run_root dasall-cli readiness --json --timeout-ms "$TIMEOUT_MS")
  assert_readiness_ready "$readiness_json" "iteration ${label} readiness"
  write_artifact_file "iteration-${label}-readiness.json" "$readiness_json"

  diag_json=$(run_root dasall-cli diag health --json)
  assert_json_contains "$diag_json" '"disposition":"completed"' "iteration ${label} diag health"
  assert_json_contains "$diag_json" 'diagnostics redacted health snapshot' "iteration ${label} diag health summary"
  write_artifact_file "iteration-${label}-diag-health.json" "$diag_json"
}

run_binary_check() {
  log_label=$1
  relpath=$2
  description=$3
  binary_path=${BUILD_DIR}/${relpath}

  [ -x "$binary_path" ] || fail "missing ${description} binary: ${binary_path}"
  log "running ${description}"
  if "$binary_path" > "$ARTIFACT_DIR/${log_label}.log" 2>&1; then
    :
  else
    fail "${description} failed; see $ARTIFACT_DIR/${log_label}.log"
  fi
}

write_summary() {
  python3 - "$ARTIFACT_DIR" "$BUILD_DIR" "$ITERATIONS" "$PROFILE_ID" "$PACKAGE_SMOKE_ARTIFACT_DIR" <<'PY'
import json
import pathlib
import sys

artifact_dir = pathlib.Path(sys.argv[1])
build_dir = sys.argv[2]
iterations = int(sys.argv[3])
profile_id = sys.argv[4]
package_smoke_dir = sys.argv[5]

summary = {
    'effective_profile_id': profile_id,
    'build_dir': build_dir,
    'package_smoke_artifact_dir': package_smoke_dir,
    'iterations_requested': iterations,
    'iterations_completed': iterations,
    'diag_enabled_apply_artifact': 'config-apply.json',
    'installed_iteration_artifacts': [
        {
            'label': f'{index:02d}',
            'readiness_artifact': f'iteration-{index:02d}-readiness.json',
            'diag_health_artifact': f'iteration-{index:02d}-diag-health.json',
        }
        for index in range(1, iterations + 1)
    ],
    'focused_binary_logs': {
        'diagnostics_smoke': 'diagnostics-smoke.log',
        'diagnostics_integration': 'diagnostics-integration.log',
        'health_wiring': 'health-wiring.log',
        'health_cadence': 'health-cadence.log',
        'secret_failure_injection': 'secret-failure.log',
        'plugin_safe_unload': 'plugin-safe-unload.log',
        'metrics_failure_injection': 'metrics-failure.log',
    },
    'covered_slices': [
        'diagnostics_retained_snapshot',
        'installed_readiness_positive',
        'installed_diag_health_positive',
        'health_probe_wiring',
        'health_cadence_projection',
        'secret_backend_unavailable',
        'secret_audit_sink_failure',
        'plugin_safe_unload',
        'observability_sink_failure',
    ],
    'release_runner_local_artifact_ready': True,
    'qemu_required_for_this_gate': False,
}

(artifact_dir / 'infra-release-soak-summary.json').write_text(
    json.dumps(summary, indent=2) + '\n',
    encoding='ascii',
)
PY
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
    --timeout-ms)
      [ "$#" -ge 2 ] || fail 'missing value for --timeout-ms'
      TIMEOUT_MS=$2
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

require_command grep
require_command mktemp
require_command python3
ensure_positive_integer "$ITERATIONS" 'iterations'
ensure_positive_integer "$TIMEOUT_MS" 'timeout-ms'
ensure_artifact_dir

if [ -n "$PACKAGE_SMOKE_ARTIFACT_DIR" ]; then
  [ -d "$PACKAGE_SMOKE_ARTIFACT_DIR" ] || fail "missing package smoke artifact directory: $PACKAGE_SMOKE_ARTIFACT_DIR"
fi

prepare_desired_state
apply_diag_enabled_config
READY_JSON=$(wait_for_daemon_ready)
write_artifact_file 'ready.json' "$READY_JSON"

iteration=1
while [ "$iteration" -le "$ITERATIONS" ]; do
  label=$(printf '%02d' "$iteration")
  log "running infra installed iteration ${iteration}/${ITERATIONS}"
  run_installed_iteration "$label"
  iteration=$((iteration + 1))
done

run_binary_check diagnostics-smoke tests/integration/infra/dasall_infra_diagnostics_smoke_integration_test 'InfraDiagnosticsSmokeTest'
run_binary_check diagnostics-integration tests/integration/infra/dasall_infra_diagnostics_integration_test 'InfraDiagnosticsIntegrationTest'
run_binary_check health-wiring tests/integration/infra/health/dasall_health_wiring_integration_test 'HealthWiringIntegrationTest'
run_binary_check health-cadence tests/integration/infra/health/dasall_infra_health_cadence_integration_test 'InfraHealthCadenceIntegrationTest'
run_binary_check secret-failure tests/integration/infra/secret/dasall_secret_failure_injection_integration_test 'SecretFailureInjectionTest'
run_binary_check plugin-safe-unload tests/unit/infra/plugin/dasall_plugin_lifecycle_state_unit_test 'PluginLifecycleStateTest'
run_binary_check metrics-failure tests/integration/infra/metrics/dasall_metrics_failure_injection_integration_test 'MetricsFailureInjectionTest'

write_summary
printf '%s\n' 'infra release / soak gate passed' > "$ARTIFACT_DIR/status.txt"
log "wrote infra release / soak artifacts to ${ARTIFACT_DIR}"