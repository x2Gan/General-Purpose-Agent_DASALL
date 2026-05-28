#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "${SCRIPT_DIR}/../.." && pwd)
ARTIFACT_DIR=$(CDPATH= cd -- "${REPO_ROOT}/.." && pwd)
ARCH=$(dpkg --print-architecture)
VERSION=$(cd "${REPO_ROOT}" && dpkg-parsechangelog -SVersion)
INSTALLED_STATE_ROOT=/var/lib/dasall
INSTALLED_RUNTIME_LOG_PATH=${INSTALLED_STATE_ROOT}/logging/runtime.log
RUNTIME_TOOL_POSITIVE_LOG_PATH=${INSTALLED_STATE_ROOT}/tool-positive/logging/runtime.log
RUNTIME_RECOVERY_POSITIVE_LOG_PATH=${INSTALLED_STATE_ROOT}/recovery-positive/logging/runtime.log
RUNTIME_RECOVERY_NEGATIVE_LOG_PATH=${INSTALLED_STATE_ROOT}/recovery-negative/logging/runtime.log

COMMON_DEB="${ARTIFACT_DIR}/dasall-common_${VERSION}_all.deb"
CLI_DEB="${ARTIFACT_DIR}/dasall-cli_${VERSION}_${ARCH}.deb"
DAEMON_DEB="${ARTIFACT_DIR}/dasall-daemon_${VERSION}_${ARCH}.deb"
META_DEB="${ARTIFACT_DIR}/dasall_${VERSION}_all.deb"
GATEWAY_BINARY_PATH=/usr/sbin/dasall-gateway
GATEWAY_HTTP_PROOF_PROFILE_ID=desktop_full
LLM_SECRET_PATH=/var/lib/dasall/secrets/llm/providers/deepseek-prod.secret
LLM_PROVIDER_MANIFEST_PATH=/usr/share/dasall/llm/providers/deepseek/manifest.yaml
MEMORY_DB_PATH=/var/lib/dasall/memory/memory.db
MEMORY_MAINTENANCE_PROOF_TOOL=/usr/lib/dasall/dasall-memory-maintenance-proof
TOOLS_INSTALLED_PROOF_TOOL=/usr/lib/dasall/dasall-tools-installed-proof
RUNTIME_INSTALLED_PROOF_TOOL=/usr/lib/dasall/dasall-runtime-installed-proof
SERVICES_INSTALLED_PROOF_SCRIPT=${SCRIPT_DIR}/services_local_installed_proof.sh
PACKAGE_SMOKE_ARTIFACT_DIR=${DASALL_PACKAGE_SMOKE_ARTIFACT_DIR:-}
SECRET_CONSUMER_MATRIX_PATH=/usr/share/dasall/docs/ssot/SecretConsumerMatrix.md
PRESERVED_SECRET_ROOT=
SECRET_PROVISIONING_MODE=uninitialized
SECRET_IMPORT_APPLY_JSON=
ASYNC_RECEIPT_PROOF_ROOT=
ASYNC_RECEIPT_PROOF_PID=
ASYNC_RECEIPT_PROOF_SOCKET=
ASYNC_RECEIPT_PROOF_LOG=
ASYNC_RECEIPT_PROOF_ARTIFACT_ROOT=
ASYNC_RECEIPT_PROOF_STATE_ROOT=
GATEWAY_HTTP_PROOF_ROOT=
GATEWAY_HTTP_PROOF_PID=
GATEWAY_HTTP_PROOF_PORT=
GATEWAY_HTTP_PROOF_LOG=
GATEWAY_HTTP_PROOF_STATE_ROOT=
LOGGING_PROOF_START_TS_MS=

log() {
  printf '[pkg-smoke-install] %s\n' "$*"
}

fail() {
  printf '[pkg-smoke-install] %s\n' "$*" >&2
  exit 1
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || fail "missing required command: $1"
}

require_artifact() {
  [ -f "$1" ] || fail "missing package artifact: $1"
}

require_root_access() {
  if [ "$(id -u)" -eq 0 ]; then
    return 0
  fi

  command -v sudo >/dev/null 2>&1 || fail 'root or sudo is required'

  if [ "${DASALL_ALLOW_INTERACTIVE_SUDO:-0}" = "1" ]; then
    sudo -v >/dev/null 2>&1 || fail 'sudo validation failed'
    return 0
  fi

  sudo -n true >/dev/null 2>&1 || fail 'passwordless sudo is required for automated lifecycle smoke; rerun as root or set DASALL_ALLOW_INTERACTIVE_SUDO=1'
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

run_dasall_cli() {
  require_root_access
  if [ "$(id -u)" -eq 0 ]; then
    require_command runuser
    runuser -u dasall -- "$@"
  else
    sudo -u dasall "$@"
  fi
}

assert_json_contains() {
  json_payload=$1
  expected=$2
  label=$3

  printf '%s\n' "$json_payload" | grep -Fq "$expected" || \
    fail "${label}: expected JSON fragment not found: ${expected}"
}

assert_json_matches() {
  json_payload=$1
  expected_regex=$2
  label=$3

  printf '%s\n' "$json_payload" | grep -Eq "$expected_regex" || \
    fail "${label}: expected JSON pattern not found: ${expected_regex}"
}

assert_text_contains() {
  text_payload=$1
  expected=$2
  label=$3

  printf '%s\n' "$text_payload" | grep -Fq "$expected" || \
    fail "${label}: expected text fragment not found: ${expected}"
}

assert_text_matches() {
  text_payload=$1
  expected_regex=$2
  label=$3

  printf '%s\n' "$text_payload" | grep -Eq "$expected_regex" || \
    fail "${label}: expected text pattern not found: ${expected_regex}"
}

json_contains_fragment() {
  json_payload=$1
  expected=$2

  printf '%s\n' "$json_payload" | grep -Fq "$expected"
}

knowledge_health_is_ready() {
  json_payload=$1

  json_contains_fragment "$json_payload" '"disposition":"completed"' || return 1
  json_contains_fragment "$json_payload" '\"operation\":\"health\"' || return 1
  json_contains_fragment "$json_payload" '\"active_snapshot_id\":\"snapshot:' || return 1

  if json_contains_fragment "$json_payload" '\"last_refresh_status\":'; then
    json_contains_fragment "$json_payload" '\"last_refresh_status\":\"completed\"' || return 1
    json_contains_fragment "$json_payload" '\"refresh_in_flight\":false' || return 1
    return 0
  fi

  json_contains_fragment "$json_payload" '\"freshness_state\":\"fresh\"'
}

assert_knowledge_health_ready() {
  json_payload=$1
  label=$2

  knowledge_health_is_ready "$json_payload" || fail "${label}: health payload did not reach a ready state: ${json_payload}"
}

assert_non_empty() {
  value=$1
  label=$2

  [ -n "$value" ] || fail "${label}: expected a non-empty value"
}

assert_min_integer() {
  value=$1
  minimum=$2
  label=$3

  case "$value" in
    ''|*[!0-9]*) fail "${label}: expected a non-negative integer, got ${value}" ;;
  esac
  [ "$value" -ge "$minimum" ] || fail "${label}: expected at least ${minimum}, got ${value}"
}

ensure_artifact_dir() {
  [ -n "$PACKAGE_SMOKE_ARTIFACT_DIR" ] || return 0
  mkdir -p "$PACKAGE_SMOKE_ARTIFACT_DIR"
}

write_artifact_file() {
  file_name=$1
  file_content=$2

  [ -n "$PACKAGE_SMOKE_ARTIFACT_DIR" ] || return 0
  ensure_artifact_dir
  printf '%s\n' "$file_content" > "$PACKAGE_SMOKE_ARTIFACT_DIR/$file_name"
}

capture_root_file_to_artifact() {
  artifact_name=$1
  source_path=$2

  [ -n "$PACKAGE_SMOKE_ARTIFACT_DIR" ] || return 0
  ensure_artifact_dir
  run_root test -f "$source_path" || fail "missing installed artifact source: $source_path"
  run_root cat "$source_path" > "$PACKAGE_SMOKE_ARTIFACT_DIR/$artifact_name"
}

wait_for_path() {
  target_path=$1
  attempt=0
  while [ "$attempt" -lt 30 ]; do
    if run_root test -f "$target_path"; then
      return 0
    fi
    attempt=$((attempt + 1))
    sleep 1
  done

  return 1
}

json_extract_string() {
  json_payload=$1
  field_name=$2

  python3 - "$field_name" "$json_payload" <<'PY'
import json
import sys

field_name = sys.argv[1]
payload = json.loads(sys.argv[2])
value = payload.get(field_name)
if isinstance(value, str):
    print(value)
PY
}

reserve_loopback_port() {
  python3 <<'PY'
import socket

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
    sock.bind(("127.0.0.1", 0))
    print(sock.getsockname()[1])
PY
}

terminate_installed_gateway_http_proof() {
  [ -n "$GATEWAY_HTTP_PROOF_PID" ] || return 0

  run_root kill "$GATEWAY_HTTP_PROOF_PID" >/dev/null 2>&1 || true

  attempt=0
  while [ "$attempt" -lt 30 ]; do
    if ! run_root kill -0 "$GATEWAY_HTTP_PROOF_PID" >/dev/null 2>&1; then
      GATEWAY_HTTP_PROOF_PID=
      return 0
    fi
    attempt=$((attempt + 1))
    sleep 1
  done

  run_root kill -9 "$GATEWAY_HTTP_PROOF_PID" >/dev/null 2>&1 || true
  GATEWAY_HTTP_PROOF_PID=
}

stop_installed_gateway_http_proof() {
  terminate_installed_gateway_http_proof

  if [ -n "$GATEWAY_HTTP_PROOF_ROOT" ]; then
    run_root rm -rf "$GATEWAY_HTTP_PROOF_ROOT" >/dev/null 2>&1 || true
  fi

  GATEWAY_HTTP_PROOF_ROOT=
  GATEWAY_HTTP_PROOF_PORT=
  GATEWAY_HTTP_PROOF_LOG=
  GATEWAY_HTTP_PROOF_STATE_ROOT=
}

start_installed_gateway_http_proof() {
  proof_mode=$1

  stop_installed_gateway_http_proof

  GATEWAY_HTTP_PROOF_ROOT=$(run_root mktemp -d /tmp/dasall-gateway-http-proof.XXXXXX)
  GATEWAY_HTTP_PROOF_PORT=$(reserve_loopback_port)
  GATEWAY_HTTP_PROOF_LOG="${GATEWAY_HTTP_PROOF_ROOT}/gateway.log"
  GATEWAY_HTTP_PROOF_STATE_ROOT="${GATEWAY_HTTP_PROOF_ROOT}/state"
  GATEWAY_HTTP_PROOF_PID_FILE="${GATEWAY_HTTP_PROOF_ROOT}/gateway.pid"

  run_root install -d -o dasall -g dasall -m 700 \
    "$GATEWAY_HTTP_PROOF_ROOT" \
    "$GATEWAY_HTTP_PROOF_STATE_ROOT"

  gateway_extra_env=
  if [ "$proof_mode" = 'missing-backend' ]; then
    gateway_extra_env='DASALL_GATEWAY_FORCE_MISSING_RUNTIME_DISPATCH_BACKEND=1'
  fi

  run_root_sh "runuser -u dasall -- env DASALL_RUNTIME_STATE_ROOT_OVERRIDE='$GATEWAY_HTTP_PROOF_STATE_ROOT' ${gateway_extra_env} '$GATEWAY_BINARY_PATH' --profile-id '$GATEWAY_HTTP_PROOF_PROFILE_ID' --port '$GATEWAY_HTTP_PROOF_PORT' >'$GATEWAY_HTTP_PROOF_LOG' 2>&1 & echo \$! >'$GATEWAY_HTTP_PROOF_PID_FILE'"
  GATEWAY_HTTP_PROOF_PID=$(run_root cat "$GATEWAY_HTTP_PROOF_PID_FILE")
  assert_non_empty "$GATEWAY_HTTP_PROOF_PID" 'installed gateway http proof pid'
}

wait_for_installed_gateway_http_ready() {
  proof_port=$1
  log_path=$2

  attempt=0
  while [ "$attempt" -lt 30 ]; do
    ready_body=$(curl -fsS --max-time 2 "http://127.0.0.1:${proof_port}/health/ready" 2>/dev/null || true)
    if [ -n "$ready_body" ] && printf '%s\n' "$ready_body" | grep -Fq 'READY'; then
      printf '%s\n' "$ready_body"
      return 0
    fi

    attempt=$((attempt + 1))
    sleep 1
  done

  run_root cat "$log_path" >&2 || true
  return 1
}

wait_for_installed_gateway_http_exit() {
  proof_pid=$1
  log_path=$2

  attempt=0
  while [ "$attempt" -lt 30 ]; do
    if ! run_root kill -0 "$proof_pid" >/dev/null 2>&1; then
      return 0
    fi

    attempt=$((attempt + 1))
    sleep 1
  done

  run_root cat "$log_path" >&2 || true
  return 1
}

verify_installed_gateway_http_flow() {
  require_command curl

  start_installed_gateway_http_proof positive

  GATEWAY_READY_BODY=$(wait_for_installed_gateway_http_ready "$GATEWAY_HTTP_PROOF_PORT" "$GATEWAY_HTTP_PROOF_LOG") || \
    fail 'installed gateway http proof did not become ready'
  assert_text_contains "$GATEWAY_READY_BODY" 'runtime_readiness=default-ready' 'installed gateway readiness detail'
  printf '%s\n' "$GATEWAY_READY_BODY" | grep -Fq 'stub-ready' && \
    fail "installed gateway readiness should not expose stub-ready: ${GATEWAY_READY_BODY}"

  GATEWAY_SUBMIT_JSON=$(curl -fsS --max-time 120 \
    -H 'Content-Type: application/json' \
    -X POST \
    --data '{"packet_id":"acc-fix-002-installed-gateway","entry_type":"gateway","peer_ref":"jwt:user://tenant-a/alice","payload":"installed gateway smoke","trace_id":"acc-fix-002-installed-gateway-trace","session_hint":"acc-fix-002-installed-gateway-session"}' \
    "http://127.0.0.1:${GATEWAY_HTTP_PROOF_PORT}/v1/submit")
  assert_json_contains "$GATEWAY_SUBMIT_JSON" '"status":"200"' 'installed gateway unary submit'
  assert_json_matches "$GATEWAY_SUBMIT_JSON" '"result_id":"[^"]+"' 'installed gateway unary result_id'
  assert_json_matches "$GATEWAY_SUBMIT_JSON" '"payload":"[^"]+' 'installed gateway unary payload'

  terminate_installed_gateway_http_proof
  GATEWAY_POSITIVE_LOG=$(run_root cat "$GATEWAY_HTTP_PROOF_LOG")
  assert_text_contains "$GATEWAY_POSITIVE_LOG" '[dasall_gateway] runtime readiness=default-ready' 'installed gateway runtime readiness log'
  assert_text_contains "$GATEWAY_POSITIVE_LOG" "[dasall_gateway] listening on :${GATEWAY_HTTP_PROOF_PORT}" 'installed gateway listen log'
  assert_text_contains "$GATEWAY_POSITIVE_LOG" '[dasall_gateway] stopped' 'installed gateway graceful stop log'
  stop_installed_gateway_http_proof

  start_installed_gateway_http_proof missing-backend
  wait_for_installed_gateway_http_exit "$GATEWAY_HTTP_PROOF_PID" "$GATEWAY_HTTP_PROOF_LOG" || \
    fail 'installed gateway missing-backend proof did not exit'
  GATEWAY_HTTP_PROOF_PID=

  set +e
  GATEWAY_NEGATIVE_READY_OUTPUT=$(curl -fsS --max-time 2 "http://127.0.0.1:${GATEWAY_HTTP_PROOF_PORT}/health/ready" 2>&1)
  GATEWAY_NEGATIVE_READY_CODE=$?
  set -e
  [ "$GATEWAY_NEGATIVE_READY_CODE" -ne 0 ] || \
    fail "installed gateway missing-backend proof should not expose a ready listener: ${GATEWAY_NEGATIVE_READY_OUTPUT}"

  GATEWAY_NEGATIVE_LOG=$(run_root cat "$GATEWAY_HTTP_PROOF_LOG")
  assert_text_contains "$GATEWAY_NEGATIVE_LOG" 'stage=access-gateway-init' 'installed gateway missing-backend startup stage'
  assert_text_contains "$GATEWAY_NEGATIVE_LOG" 'detail=production submit pipeline unavailable' 'installed gateway missing-backend fail-closed detail'
  printf '%s\n' "$GATEWAY_NEGATIVE_LOG" | grep -Fq '[dasall_gateway] listening on :' && \
    fail "installed gateway missing-backend proof should fail before listen: ${GATEWAY_NEGATIVE_LOG}"

  if [ -n "$PACKAGE_SMOKE_ARTIFACT_DIR" ]; then
    python3 - "$PACKAGE_SMOKE_ARTIFACT_DIR/access-installed-gateway-http-proof.json" \
      "$GATEWAY_BINARY_PATH" \
      "$GATEWAY_HTTP_PROOF_PROFILE_ID" \
      "$GATEWAY_READY_BODY" \
      "$GATEWAY_SUBMIT_JSON" \
      "$GATEWAY_POSITIVE_LOG" \
      "$GATEWAY_NEGATIVE_LOG" <<'PY'
import json
import sys

path = sys.argv[1]
data = {
  'gateway_binary_path': sys.argv[2],
  'effective_profile_id': sys.argv[3],
  'ready_body': sys.argv[4],
  'submit': json.loads(sys.argv[5]),
  'positive_log': sys.argv[6],
  'negative_log': sys.argv[7],
  'negative_listener_exposed': False,
  'non_extrapolation': [
    'This artifact proves local installed HTTP gateway unary positive evidence only.',
    'It does not imply multi-instance authority, streaming readiness, or qemu/release-runner closure.',
    'The missing-backend negative is env-gated for package smoke and does not widen the public HTTP surface.',
  ],
}
with open(path, 'w', encoding='ascii') as handle:
  json.dump(data, handle, indent=2)
  handle.write('\n')
PY
  fi

  stop_installed_gateway_http_proof
}

stop_async_receipt_proof_daemon() {
  if [ -n "$ASYNC_RECEIPT_PROOF_PID" ]; then
    run_root kill "$ASYNC_RECEIPT_PROOF_PID" >/dev/null 2>&1 || true
    ASYNC_RECEIPT_PROOF_PID=
  fi

  if [ -n "$ASYNC_RECEIPT_PROOF_ROOT" ]; then
    run_root rm -rf "$ASYNC_RECEIPT_PROOF_ROOT" >/dev/null 2>&1 || true
    ASYNC_RECEIPT_PROOF_ROOT=
    ASYNC_RECEIPT_PROOF_SOCKET=
    ASYNC_RECEIPT_PROOF_LOG=
    ASYNC_RECEIPT_PROOF_ARTIFACT_ROOT=
    ASYNC_RECEIPT_PROOF_STATE_ROOT=
  fi
}

wait_for_async_receipt_proof_daemon_ready() {
  socket_path=$1
  log_path=$2
  attempt=0
  while [ "$attempt" -lt 30 ]; do
    if run_root test -S "$socket_path" &&
       run_dasall_cli dasall-cli --socket-path "$socket_path" ping --json >/dev/null 2>&1 &&
       run_dasall_cli dasall-cli --socket-path "$socket_path" readiness --json >/dev/null 2>&1; then
      return 0
    fi

    attempt=$((attempt + 1))
    sleep 1
  done

  run_root cat "$log_path" >&2 || true
  return 1
}

start_async_receipt_proof_daemon() {
  proof_daemon_profile_id=$1

  stop_async_receipt_proof_daemon

  ASYNC_RECEIPT_PROOF_ROOT=$(run_root mktemp -d /tmp/dasall-async-receipt-proof.XXXXXX)
  ASYNC_RECEIPT_PROOF_SOCKET="${ASYNC_RECEIPT_PROOF_ROOT}/daemon.sock"
  ASYNC_RECEIPT_PROOF_LOG="${ASYNC_RECEIPT_PROOF_ROOT}/daemon.log"
  ASYNC_RECEIPT_PROOF_ARTIFACT_ROOT="${ASYNC_RECEIPT_PROOF_ROOT}/artifacts"
  ASYNC_RECEIPT_PROOF_STATE_ROOT="${ASYNC_RECEIPT_PROOF_ROOT}/state"
  ASYNC_RECEIPT_PROOF_PID_FILE="${ASYNC_RECEIPT_PROOF_ROOT}/daemon.pid"
  ASYNC_RECEIPT_PROOF_CONFIG_FILE="${ASYNC_RECEIPT_PROOF_ROOT}/daemon.json"

  run_root install -d -o dasall -g dasall -m 700 \
    "$ASYNC_RECEIPT_PROOF_ROOT" \
    "$ASYNC_RECEIPT_PROOF_ARTIFACT_ROOT" \
    "$ASYNC_RECEIPT_PROOF_STATE_ROOT"

  run_root python3 - /etc/dasall/daemon.json "$ASYNC_RECEIPT_PROOF_CONFIG_FILE" "$ASYNC_RECEIPT_PROOF_SOCKET" <<'PY'
import json
import sys

source_path = sys.argv[1]
target_path = sys.argv[2]
socket_path = sys.argv[3]

with open(source_path, encoding='utf-8') as handle:
    payload = json.load(handle)

daemon_config = payload.setdefault('daemon', {})
daemon_config['socket_path'] = socket_path

with open(target_path, 'w', encoding='utf-8') as handle:
    json.dump(payload, handle, indent=2)
    handle.write('\n')
PY
  run_root chown dasall:dasall "$ASYNC_RECEIPT_PROOF_CONFIG_FILE"
  run_root chmod 600 "$ASYNC_RECEIPT_PROOF_CONFIG_FILE"

  run_root_sh "runuser -u dasall -- env DASALL_DAEMON_ASYNC_RECEIPT_PROOF_DIR='$ASYNC_RECEIPT_PROOF_ARTIFACT_ROOT' DASALL_RUNTIME_STATE_ROOT_OVERRIDE='$ASYNC_RECEIPT_PROOF_STATE_ROOT' /usr/sbin/dasall-daemon --profile-id '$proof_daemon_profile_id' --config-file '$ASYNC_RECEIPT_PROOF_CONFIG_FILE' >'$ASYNC_RECEIPT_PROOF_LOG' 2>&1 & echo \$! >'$ASYNC_RECEIPT_PROOF_PID_FILE'"
  ASYNC_RECEIPT_PROOF_PID=$(run_root cat "$ASYNC_RECEIPT_PROOF_PID_FILE")
  assert_non_empty "$ASYNC_RECEIPT_PROOF_PID" 'async receipt proof daemon pid'
  wait_for_async_receipt_proof_daemon_ready "$ASYNC_RECEIPT_PROOF_SOCKET" "$ASYNC_RECEIPT_PROOF_LOG" || \
    fail 'async receipt proof daemon did not become ready'
}

verify_installed_async_receipt_flow() {
  proof_profile_id=$(run_root_sh '. /etc/default/dasall-daemon && : "${DASALL_DAEMON_PROFILE_ID:?missing DASALL_DAEMON_PROFILE_ID}" && printf "%s" "${DASALL_DAEMON_PROFILE_ID}"')
  proof_request_id='acc-fix-001-installed-async-proof'
  proof_request='{"prompt":"installed async receipt positive proof"}'

  start_async_receipt_proof_daemon "$proof_profile_id"

  PROOF_SUBMIT_JSON=$(run_dasall_cli dasall-cli --socket-path "$ASYNC_RECEIPT_PROOF_SOCKET" run "$proof_request" --async --request-id "$proof_request_id" --json --timeout-ms 30000)
  assert_json_contains "$PROOF_SUBMIT_JSON" '"disposition":"accepted_async"' 'installed async receipt submit'
  PROOF_RECEIPT_REF=$(json_extract_string "$PROOF_SUBMIT_JSON" receipt_ref)
  assert_non_empty "$PROOF_RECEIPT_REF" 'installed async receipt submit receipt_ref'

  PROOF_RECEIPT_FILE="${ASYNC_RECEIPT_PROOF_ARTIFACT_ROOT}/${proof_request_id}.json"
  wait_for_path "$PROOF_RECEIPT_FILE" || fail 'async receipt proof metadata file was not written'
  PROOF_RECEIPT_JSON=$(run_root cat "$PROOF_RECEIPT_FILE")
  PROOF_OWNERSHIP_TOKEN=$(json_extract_string "$PROOF_RECEIPT_JSON" ownership_token)
  PROOF_ACTOR_REF=$(json_extract_string "$PROOF_RECEIPT_JSON" actor_ref)
  assert_non_empty "$PROOF_OWNERSHIP_TOKEN" 'installed async receipt ownership_token'
  assert_non_empty "$PROOF_ACTOR_REF" 'installed async receipt actor_ref'

  PROOF_STATUS_JSON=$(run_dasall_cli dasall-cli --socket-path "$ASYNC_RECEIPT_PROOF_SOCKET" status "$PROOF_RECEIPT_REF" "$PROOF_OWNERSHIP_TOKEN" "$PROOF_ACTOR_REF" --json --timeout-ms 30000)
  assert_json_contains "$PROOF_STATUS_JSON" '"disposition":"completed"' 'installed async receipt status'
  assert_json_contains "$PROOF_STATUS_JSON" '"response_text":"active"' 'installed async receipt active status'

  PROOF_REPLAY_JSON=$(run_dasall_cli dasall-cli --socket-path "$ASYNC_RECEIPT_PROOF_SOCKET" run "$proof_request" --async --request-id "$proof_request_id" --json --timeout-ms 30000)
  assert_json_contains "$PROOF_REPLAY_JSON" '"disposition":"accepted_async"' 'installed async receipt replay submit'
  assert_json_contains "$PROOF_REPLAY_JSON" "\"receipt_ref\":\"${PROOF_RECEIPT_REF}\"" 'installed async receipt replay receipt_ref'

  set +e
  PROOF_STATUS_MISMATCH_JSON=$(run_dasall_cli dasall-cli --socket-path "$ASYNC_RECEIPT_PROOF_SOCKET" status "$PROOF_RECEIPT_REF" "$PROOF_OWNERSHIP_TOKEN" 'local://uid/0' --json --timeout-ms 30000 2>&1)
  PROOF_STATUS_MISMATCH_CODE=$?
  set -e
  [ "$PROOF_STATUS_MISMATCH_CODE" -eq 4 ] || fail "installed async receipt status owner mismatch should exit 4, got ${PROOF_STATUS_MISMATCH_CODE}: ${PROOF_STATUS_MISMATCH_JSON}"
  assert_json_contains "$PROOF_STATUS_MISMATCH_JSON" '"error_ref":"status_owner_mismatch"' 'installed async receipt status owner mismatch'

  set +e
  PROOF_CANCEL_MISMATCH_JSON=$(run_root dasall-cli --socket-path "$ASYNC_RECEIPT_PROOF_SOCKET" cancel "$PROOF_RECEIPT_REF" "$PROOF_OWNERSHIP_TOKEN" --json --timeout-ms 30000 2>&1)
  PROOF_CANCEL_MISMATCH_CODE=$?
  set -e
  [ "$PROOF_CANCEL_MISMATCH_CODE" -eq 4 ] || fail "installed async receipt cancel owner mismatch should exit 4, got ${PROOF_CANCEL_MISMATCH_CODE}: ${PROOF_CANCEL_MISMATCH_JSON}"
  assert_json_contains "$PROOF_CANCEL_MISMATCH_JSON" '"error_ref":"cancel_owner_mismatch"' 'installed async receipt cancel owner mismatch'

  PROOF_CANCEL_JSON=$(run_dasall_cli dasall-cli --socket-path "$ASYNC_RECEIPT_PROOF_SOCKET" cancel "$PROOF_RECEIPT_REF" "$PROOF_OWNERSHIP_TOKEN" "$PROOF_ACTOR_REF" --json --timeout-ms 30000)
  assert_json_contains "$PROOF_CANCEL_JSON" '"disposition":"completed"' 'installed async receipt cancel'
  assert_json_contains "$PROOF_CANCEL_JSON" '"response_text":"cancelled"' 'installed async receipt cancel status'

  PROOF_CANCELLED_STATUS_JSON=$(run_dasall_cli dasall-cli --socket-path "$ASYNC_RECEIPT_PROOF_SOCKET" status "$PROOF_RECEIPT_REF" "$PROOF_OWNERSHIP_TOKEN" "$PROOF_ACTOR_REF" --json --timeout-ms 30000)
  assert_json_contains "$PROOF_CANCELLED_STATUS_JSON" '"disposition":"completed"' 'installed async receipt cancelled status'
  assert_json_contains "$PROOF_CANCELLED_STATUS_JSON" '"response_text":"cancelled"' 'installed async receipt cancelled projection'

  if [ -n "$PACKAGE_SMOKE_ARTIFACT_DIR" ]; then
    python3 - "$PACKAGE_SMOKE_ARTIFACT_DIR/access-installed-async-receipt-proof.json" \
      "$PROOF_RECEIPT_REF" \
      "$PROOF_ACTOR_REF" \
      "$PROOF_SUBMIT_JSON" \
      "$PROOF_STATUS_JSON" \
      "$PROOF_REPLAY_JSON" \
      "$PROOF_STATUS_MISMATCH_JSON" \
      "$PROOF_CANCEL_MISMATCH_JSON" \
      "$PROOF_CANCEL_JSON" \
      "$PROOF_CANCELLED_STATUS_JSON" <<'PY'
import json
import sys

path = sys.argv[1]
data = {
  "receipt_ref": sys.argv[2],
  "actor_ref": sys.argv[3],
  "submit": json.loads(sys.argv[4]),
  "status_active": json.loads(sys.argv[5]),
  "replay": json.loads(sys.argv[6]),
  "status_owner_mismatch": json.loads(sys.argv[7]),
  "cancel_owner_mismatch": json.loads(sys.argv[8]),
  "cancel": json.loads(sys.argv[9]),
  "status_after_cancel": json.loads(sys.argv[10]),
}
with open(path, 'w', encoding='ascii') as handle:
    json.dump(data, handle, indent=2)
    handle.write('\n')
PY
  fi

  stop_async_receipt_proof_daemon
}

write_secret_consumer_package_proof() {
  [ -n "$PACKAGE_SMOKE_ARTIFACT_DIR" ] || return 0

  run_root test -f "$SECRET_CONSUMER_MATRIX_PATH"
  run_root test -f "$LLM_PROVIDER_MANIFEST_PATH"
  run_root_sh "grep -Fqx 'auth_ref: secret://llm/providers/deepseek-prod' '$LLM_PROVIDER_MANIFEST_PATH'"

  secret_record_owner=$(run_root_sh "stat -c '%U' '$LLM_SECRET_PATH'")
  secret_record_group=$(run_root_sh "stat -c '%G' '$LLM_SECRET_PATH'")
  secret_record_mode=$(run_root_sh "stat -c '%a' '$LLM_SECRET_PATH'")
  secret_root_mode=$(run_root_sh "stat -c '%a' /var/lib/dasall/secrets")
  provider_manifest_auth_ref=$(run_root_sh "grep -F 'auth_ref:' '$LLM_PROVIDER_MANIFEST_PATH' | head -n 1")

  written_secret_ref_observed=false
  if [ -n "$SECRET_IMPORT_APPLY_JSON" ] &&
   json_contains_fragment "$SECRET_IMPORT_APPLY_JSON" '"written_secret_refs":["secret://llm/providers/deepseek-prod"]'; then
  written_secret_ref_observed=true
  fi

  python3 - "$PACKAGE_SMOKE_ARTIFACT_DIR/secret-consumer-package-proof.json" \
  "$SECRET_CONSUMER_MATRIX_PATH" \
  "$LLM_PROVIDER_MANIFEST_PATH" \
  "$provider_manifest_auth_ref" \
  "$SECRET_PROVISIONING_MODE" \
  "$written_secret_ref_observed" \
  "$LLM_SECRET_PATH" \
  "$secret_record_owner" \
  "$secret_record_group" \
  "$secret_record_mode" \
  "$secret_root_mode" <<'PY'
import json
import sys

def parse_bool(text: str) -> bool:
  return text.lower() == "true"

path = sys.argv[1]
data = {
  "scope": "local-installed-package",
  "matrix_doc_path": sys.argv[2],
  "matrix_doc_present": True,
  "provider_manifest_path": sys.argv[3],
  "provider_manifest_present": True,
  "provider_manifest_auth_ref_line": sys.argv[4],
  "bootstrap_provisioning_mode": sys.argv[5],
  "config_apply_written_secret_ref_observed": parse_bool(sys.argv[6]),
  "secret_record_path": sys.argv[7],
  "secret_record_present": True,
  "secret_record_owner": sys.argv[8],
  "secret_record_group": sys.argv[9],
  "secret_record_mode": sys.argv[10],
  "secret_root_mode": sys.argv[11],
  "non_extrapolation": [
    "Access ownership HMAC still requires explicit ownership_token_hmac_secret_ref and is not package-enabled by default.",
    "OTA and Plugin trust anchor readers do not gain local installed verify proof from this artifact.",
    "Bootstrap import and local DeepSeek smoke do not imply broader provider runtime, qemu, or production key-management readiness.",
  ],
}

with open(path, "w", encoding="ascii") as handle:
  json.dump(data, handle, indent=2)
  handle.write("\n")
PY
}

wait_for_knowledge_refresh_ready() {
  attempts=0
  while [ "$attempts" -lt 30 ]; do
    knowledge_health_json=$(run_root dasall-cli knowledge health --json --timeout-ms 30000)
    if knowledge_health_is_ready "$knowledge_health_json"; then
      printf '%s\n' "$knowledge_health_json"
      return 0
    fi
    if json_contains_fragment "$knowledge_health_json" '"last_refresh_status":"failed"'; then
      fail "knowledge refresh terminal failure: ${knowledge_health_json}"
    fi
    attempts=$((attempts + 1))
    sleep 1
  done

  fail 'timed out waiting for async knowledge refresh completion'
}

query_sqlite_scalar() {
  db_path=$1
  sql_query=$2
  run_root python3 - "$db_path" "$sql_query" <<'PY'
import sqlite3
import sys

db_path = sys.argv[1]
sql_query = sys.argv[2]
with sqlite3.connect(db_path) as connection:
    row = connection.execute(sql_query).fetchone()
    if row is None:
        sys.exit(2)
    print(row[0])
PY
}

query_sqlite_scalar_with_params() {
  db_path=$1
  sql_query=$2
  shift 2

  run_root python3 - "$db_path" "$sql_query" "$@" <<'PY'
import re
import sqlite3
import sys

db_path = sys.argv[1]
sql_query = re.sub(r"\?[0-9]+", "?", sys.argv[2])
params = tuple(sys.argv[3:])
with sqlite3.connect(db_path) as connection:
    row = connection.execute(sql_query, params).fetchone()
    if row is None:
        sys.exit(2)
    print(row[0])
PY
}

assert_positive_integer() {
  value=$1
  label=$2
  case "$value" in
    ''|*[!0-9]*) fail "${label}: expected a non-negative integer, got ${value}" ;;
  esac
  [ "$value" -ge 1 ] || fail "${label}: expected at least one row, got ${value}"
}

cleanup() {
  stop_installed_gateway_http_proof
  stop_async_receipt_proof_daemon
  run_root systemctl disable --now dasall-daemon.service >/dev/null 2>&1 || true
  if [ -n "${PRESERVED_SECRET_ROOT}" ]; then
    run_root rm -rf "${PRESERVED_SECRET_ROOT}" >/dev/null 2>&1 || true
  fi
}

wait_for_daemon_ready() {
  attempt=0
  while [ "${attempt}" -lt 90 ]; do
    if run_root_sh 'systemctl is-enabled --quiet dasall-daemon.service >/dev/null 2>&1 &&
       systemctl is-active --quiet dasall-daemon.service >/dev/null 2>&1 &&
       [ -S /run/dasall/daemon.sock ] &&
       [ "$(stat -c "%U:%G" /run/dasall/daemon.sock 2>/dev/null)" = "dasall:dasall" ] &&
       [ "$(stat -c "%a" /run/dasall/daemon.sock 2>/dev/null)" = "600" ]' &&
      run_dasall_cli dasall-cli ping --json >/dev/null 2>&1 &&
      run_dasall_cli dasall-cli readiness --json >/dev/null 2>&1; then
      return 0
    fi

    attempt=$((attempt + 1))
    sleep 1
  done

  run_root systemctl status --no-pager dasall-daemon.service >&2 || true
  return 1
}

trap cleanup EXIT

preserve_existing_llm_secret() {
  require_root_access
  if run_root test -f "${LLM_SECRET_PATH}"; then
    PRESERVED_SECRET_ROOT=$(run_root mktemp -d /tmp/dasall-preserved-secret.XXXXXX)
    run_root chmod 700 "${PRESERVED_SECRET_ROOT}"
    run_root cp -p "${LLM_SECRET_PATH}" "${PRESERVED_SECRET_ROOT}/deepseek-prod.secret"
    log 'preserved existing DeepSeek secret for reinstall smoke'
  elif [ -n "${DASALL_DEEPSEEK_API_KEY_FILE:-}" ]; then
    [ -f "${DASALL_DEEPSEEK_API_KEY_FILE}" ] || fail 'DASALL_DEEPSEEK_API_KEY_FILE does not point to a readable file'
    PRESERVED_SECRET_ROOT=$(run_root mktemp -d /tmp/dasall-preserved-secret.XXXXXX)
    run_root chmod 700 "${PRESERVED_SECRET_ROOT}"
    run_root cp "${DASALL_DEEPSEEK_API_KEY_FILE}" "${PRESERVED_SECRET_ROOT}/deepseek-prod.key"
    run_root chmod 600 "${PRESERVED_SECRET_ROOT}/deepseek-prod.key"
    log 'preserved DeepSeek secret import file for reinstall smoke'
  fi
}

restore_preserved_llm_secret() {
  [ -n "${PRESERVED_SECRET_ROOT}" ] || fail 'missing DeepSeek secret; configure secret://llm/providers/deepseek-prod or set DASALL_DEEPSEEK_API_KEY_FILE before running LLM package smoke'

  if run_root test -f "${PRESERVED_SECRET_ROOT}/deepseek-prod.secret"; then
    run_root mkdir -p /var/lib/dasall/secrets/llm/providers
    run_root cp -p "${PRESERVED_SECRET_ROOT}/deepseek-prod.secret" "${LLM_SECRET_PATH}"
    SECRET_PROVISIONING_MODE=preserved_secret_record_copy
  else
    run_root test -f "${PRESERVED_SECRET_ROOT}/deepseek-prod.key"
    SECRET_IMPORT_APPLY_JSON=$(run_root_sh "
      . /etc/default/dasall-daemon
      : \"\${DASALL_DAEMON_PROFILE_ID:?missing DASALL_DAEMON_PROFILE_ID}\"
      cat > '${PRESERVED_SECRET_ROOT}/desired.yaml' <<EOF
schema_version: dasall.config.apply.v1
profile_id: \${DASALL_DAEMON_PROFILE_ID}
daemon:
  socket_path: /run/dasall/daemon.sock
  log_format: text
  diag_enabled: false
  override_enabled: false
  watchdog_enabled: false
service:
  start_now: false
  enable_on_boot: false
operator_access:
  add_users: []
secrets:
  refs:
    - ref: secret://llm/providers/deepseek-prod
      source: file:${PRESERVED_SECRET_ROOT}/deepseek-prod.key
      auth_profile_name: primary
EOF
      chmod 600 '${PRESERVED_SECRET_ROOT}/desired.yaml'
      dasall-cli config apply --from-file '${PRESERVED_SECRET_ROOT}/desired.yaml' --no-input --json
    ")
    SECRET_PROVISIONING_MODE=config_apply_import
    assert_json_contains "$SECRET_IMPORT_APPLY_JSON" '"written_secret_refs":["secret://llm/providers/deepseek-prod"]' 'package smoke secret import apply'
    log 'imported DeepSeek secret for reinstall smoke from DASALL_DEEPSEEK_API_KEY_FILE'
  fi

  run_root test -f "${LLM_SECRET_PATH}"
  run_root chgrp dasall /var/lib/dasall/secrets /var/lib/dasall/secrets/llm /var/lib/dasall/secrets/llm/providers "${LLM_SECRET_PATH}"
  run_root chmod 750 /var/lib/dasall/secrets /var/lib/dasall/secrets/llm /var/lib/dasall/secrets/llm/providers
  run_root chmod 640 "${LLM_SECRET_PATH}"
}

reset_existing_state() {
  log 'resetting previous DASALL install state'
  run_root_sh '
    systemctl disable --now dasall-daemon.service >/dev/null 2>&1 || true
    dpkg -P dasall dasall-daemon dasall-cli dasall-common >/dev/null 2>&1 || true
    rm -f /var/lib/dasall/pkg-smoke-state
    rm -rf /var/lib/dasall/memory
    rm -rf /var/lib/dasall/tool-positive
    rm -rf /var/lib/dasall/recovery-positive
    rm -rf /var/lib/dasall/recovery-negative
    rm -rf /var/lib/dasall/secrets
  '
}

install_packages() {
  log 'installing Debian package set'
  run_root dpkg -i "$COMMON_DEB" "$CLI_DEB" "$DAEMON_DEB" "$META_DEB"
}

verify_packages_installed() {
  for pkg in dasall dasall-cli dasall-daemon dasall-common; do
    run_root_sh "dpkg-query -W -f='\${Status}\n' ${pkg} | grep -Fqx 'install ok installed'"
  done
}

verify_service_not_started() {
  run_root_sh '! systemctl is-enabled --quiet dasall-daemon.service >/dev/null 2>&1'
  run_root_sh '! systemctl is-active --quiet dasall-daemon.service >/dev/null 2>&1'
}

verify_installed_files() {
  run_root test -f /usr/share/doc/dasall-daemon/README.Debian
  run_root test -f /etc/default/dasall-daemon
  run_root test -f /etc/dasall/daemon.json
  run_root test -x "$GATEWAY_BINARY_PATH"
  run_root test -f /usr/lib/dasall/sqlite-vss/vector0.so
  run_root test -f /usr/lib/dasall/sqlite-vss/vss0.so
  run_root test -f "$LLM_PROVIDER_MANIFEST_PATH"
  run_root test -f "$SECRET_CONSUMER_MATRIX_PATH"
  run_root_sh "grep -Fqx 'auth_ref: secret://llm/providers/deepseek-prod' '$LLM_PROVIDER_MANIFEST_PATH'"
  run_root test -x "${TOOLS_INSTALLED_PROOF_TOOL}"
  run_root test -x "${RUNTIME_INSTALLED_PROOF_TOOL}"
}

verify_validate_only() {
  log 'verifying daemon validate-only against installed defaults'
  run_root_sh '. /etc/default/dasall-daemon && : "${DASALL_DAEMON_PROFILE_ID:?missing DASALL_DAEMON_PROFILE_ID}" && /usr/sbin/dasall-daemon --validate-only --profile-id="${DASALL_DAEMON_PROFILE_ID}" --config-file /etc/dasall/daemon.json'
}

verify_tui_noninteractive_redirect() {
  set +e
  TUI_STDERR=$(dasall 2>&1)
  TUI_EXIT_CODE=$?
  set -e
  [ "$TUI_EXIT_CODE" -eq 1 ] || fail "bare dasall should fail closed without a TTY, got ${TUI_EXIT_CODE}: ${TUI_STDERR}"
  assert_text_matches "$TUI_STDERR" 'std(in|out|err) is not attached to a TTY' 'tui non-tty smoke'
  assert_text_contains "$TUI_STDERR" 'Use dasall-cli for non-interactive control-plane tasks.' 'tui non-tty redirect'
  if [ -n "$PACKAGE_SMOKE_ARTIFACT_DIR" ]; then
    write_artifact_file 'tui-noninteractive.txt' "$TUI_STDERR"
  fi
}

start_explicit_daemon_and_verify_tui_baseline() {
  restore_secret_before_start=$1

  log 'verifying daemon stays stopped until explicit service enable/start'
  run_root_sh '! systemctl is-enabled --quiet dasall-daemon.service >/dev/null 2>&1'
  run_root_sh '! systemctl is-active --quiet dasall-daemon.service >/dev/null 2>&1'

  if [ "$restore_secret_before_start" -eq 1 ]; then
    restore_preserved_llm_secret
  fi

  run_root systemctl enable --now dasall-daemon.service
  wait_for_daemon_ready

  run_root systemctl is-active --quiet dasall-daemon.service
  run_root systemctl is-enabled --quiet dasall-daemon.service
  run_root test -S /run/dasall/daemon.sock
  run_root_sh 'test "$(stat -c "%U:%G" /run/dasall/daemon.sock)" = "dasall:dasall"'
  run_root_sh 'test "$(stat -c "%a" /run/dasall/daemon.sock)" = "600"'
  verify_tui_noninteractive_redirect

  run_dasall_cli dasall-cli ping --json >/dev/null
  run_dasall_cli dasall-cli readiness --json >/dev/null
}

verify_installed_tui_daemon_backed_flow() {
  tui_smoke_profile_id=$(run_root_sh '. /etc/default/dasall-daemon && : "${DASALL_DAEMON_PROFILE_ID:?missing DASALL_DAEMON_PROFILE_ID}" && printf "%s" "${DASALL_DAEMON_PROFILE_ID}"')

  start_async_receipt_proof_daemon "$tui_smoke_profile_id"

  TUI_DAEMON_BACKED_JSON=$(run_dasall_cli env \
    DASALL_TUI_DAEMON_SOCKET="$ASYNC_RECEIPT_PROOF_SOCKET" \
    DASALL_TUI_SCRIPTED_SMOKE=daemon_roundtrip \
    DASALL_TUI_SCRIPTED_SMOKE_PROMPT='installed daemon-backed tui roundtrip' \
    DASALL_TUI_SCRIPTED_SMOKE_PROFILE_ID="$tui_smoke_profile_id" \
    dasall)

  assert_json_contains "$TUI_DAEMON_BACKED_JSON" '"mode":"daemon_roundtrip"' 'installed tui smoke mode'
  assert_json_contains "$TUI_DAEMON_BACKED_JSON" '"shutdown_clean":true' 'installed tui smoke shutdown'
  assert_json_contains "$TUI_DAEMON_BACKED_JSON" '"session_closed_cleanly":true' 'installed tui smoke close_session'
  assert_json_contains "$TUI_DAEMON_BACKED_JSON" "\"profile_id\":\"${tui_smoke_profile_id}\"" 'installed tui smoke profile id'
  assert_json_contains "$TUI_DAEMON_BACKED_JSON" '"daemon_readiness":"ready"' 'installed tui smoke readiness'
  assert_json_contains "$TUI_DAEMON_BACKED_JSON" '"current_provider_id":"daemon-local"' 'installed tui smoke provider projection'
  assert_json_contains "$TUI_DAEMON_BACKED_JSON" '"current_model_id":"dasall-core"' 'installed tui smoke model projection'
  assert_json_contains "$TUI_DAEMON_BACKED_JSON" '"status_stage":"accepted_async"' 'installed tui smoke status stage'
  assert_json_contains "$TUI_DAEMON_BACKED_JSON" '"status_current_tool":"access.submit"' 'installed tui smoke current tool'
  assert_json_contains "$TUI_DAEMON_BACKED_JSON" '"latest_transcript_content":"queued for daemon-backed execution"' 'installed tui smoke transcript receipt'
  assert_json_contains "$TUI_DAEMON_BACKED_JSON" '"latest_banner_title":"Turn submitted"' 'installed tui smoke submit banner'
  assert_json_matches "$TUI_DAEMON_BACKED_JSON" '"transcript_count":[1-9][0-9]*' 'installed tui smoke transcript count'
  assert_json_contains "$TUI_DAEMON_BACKED_JSON" '"rendered_screen_contains_receipt":true' 'installed tui smoke rendered receipt'
  assert_json_contains "$TUI_DAEMON_BACKED_JSON" '"rendered_screen_contains_route":true' 'installed tui smoke rendered route'
  write_artifact_file 'tui-daemon-backed-proof.json' "$TUI_DAEMON_BACKED_JSON"

  stop_async_receipt_proof_daemon
}

verify_tui_daemon_backed_check() {
  log 'verifying installed TUI daemon-backed smoke'
  start_explicit_daemon_and_verify_tui_baseline 0
  verify_installed_tui_daemon_backed_flow
}

verify_explicit_start() {
  start_explicit_daemon_and_verify_tui_baseline 1

  require_command python3
  ensure_artifact_dir
  LOGGING_PROOF_START_TS_MS=$(python3 - <<'PY'
import time

print(int(time.time() * 1000))
PY
)
  if [ -n "$PACKAGE_SMOKE_ARTIFACT_DIR" ]; then
    log "writing package smoke artifacts to ${PACKAGE_SMOKE_ARTIFACT_DIR}"
  fi

  verify_installed_gateway_http_flow

  MEMORY_SESSION_HINT='pkg-smoke-memory-session'
  MEMORY_EXPECTED_MARKER='mem-fix-006-local-proof'
  MEMORY_FIRST_REQUEST='{"prompt":"Remember this exact marker for this session: mem-fix-006-local-proof. Reply with that marker once and do not use any tools."}'
  MEMORY_SECOND_REQUEST='{"prompt":"In this same session, what exact marker did I ask you to remember? Reply with the exact marker once and do not use any tools."}'

  set +e
  FIRST_RUN_JSON=$(run_dasall_cli dasall-cli run "$MEMORY_FIRST_REQUEST" --session "$MEMORY_SESSION_HINT" --request-id pkg-smoke-memory-turn-001 --json --timeout-ms 120000 2>&1)
  FIRST_RUN_CODE=$?
  set -e
  [ "$FIRST_RUN_CODE" -eq 0 ] || fail "first run smoke failed: ${FIRST_RUN_JSON}"
  assert_json_contains "$FIRST_RUN_JSON" '"disposition":"completed"' 'first run smoke'
  assert_json_contains "$FIRST_RUN_JSON" '"task_completed":true' 'first run smoke'
  assert_json_contains "$FIRST_RUN_JSON" 'llm.origin=deepseek-prod/' 'first llm response payload'
  assert_json_contains "$FIRST_RUN_JSON" "$MEMORY_EXPECTED_MARKER" 'first llm marker echo'
  printf '%s\n' "$FIRST_RUN_JSON" | grep -Eq '"(tool_name|capability_id)":"agent\.dataset"' && \
    fail 'run smoke unexpectedly returned an agent.dataset tool payload instead of an LLM response'
  write_artifact_file 'run-first.json' "$FIRST_RUN_JSON"

  MEMORY_SESSION_ID=$(json_extract_string "$FIRST_RUN_JSON" session_id)
  assert_non_empty "$MEMORY_SESSION_ID" 'first run session_id'

  run_root test -f /usr/share/dasall/sql/memory/V001__initial_schema.sql
  run_root test -f "${MEMORY_DB_PATH}"
  JOURNAL_MODE=$(query_sqlite_scalar "${MEMORY_DB_PATH}" 'PRAGMA journal_mode;')
  [ "$JOURNAL_MODE" = "wal" ] || fail "memory sqlite journal mode should be wal, got ${JOURNAL_MODE}"
  MEMORY_TABLE_COUNT=$(query_sqlite_scalar "${MEMORY_DB_PATH}" "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name IN ('sessions','turns','summaries','facts','experiences');")
  [ "$MEMORY_TABLE_COUNT" -eq 5 ] || fail "memory sqlite schema should expose five core tables, got ${MEMORY_TABLE_COUNT}"
  MEMORY_VECTOR_TABLE_COUNT=$(query_sqlite_scalar "${MEMORY_DB_PATH}" "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name = 'memory_vector_documents';")
  [ "$MEMORY_VECTOR_TABLE_COUNT" -eq 1 ] || fail "memory sqlite schema should expose the vector sidecar table, got ${MEMORY_VECTOR_TABLE_COUNT}"
  MEMORY_TURN_COUNT=$(query_sqlite_scalar_with_params "${MEMORY_DB_PATH}" 'SELECT COUNT(*) FROM turns WHERE session_id = ?1 AND agent_response LIKE ?2;' "$MEMORY_SESSION_ID" 'llm.origin=deepseek-prod/%')
  assert_positive_integer "$MEMORY_TURN_COUNT" 'memory sqlite llm turn writeback'
  MEMORY_SUMMARY_COUNT=$(query_sqlite_scalar_with_params "${MEMORY_DB_PATH}" 'SELECT COUNT(*) FROM summaries WHERE session_id = ?1 AND summary_text LIKE ?2;' "$MEMORY_SESSION_ID" 'llm.origin=deepseek-prod/%')
  assert_positive_integer "$MEMORY_SUMMARY_COUNT" 'memory sqlite llm summary writeback'
  MEMORY_SESSION_SUMMARY_COUNT_AFTER_FIRST=$(query_sqlite_scalar_with_params "${MEMORY_DB_PATH}" 'SELECT COUNT(*) FROM summaries WHERE session_id = ?1;' "$MEMORY_SESSION_ID")
  assert_positive_integer "$MEMORY_SESSION_SUMMARY_COUNT_AFTER_FIRST" 'first-run session summary rows'
  MEMORY_FIRST_TURN_ID=$(query_sqlite_scalar_with_params "${MEMORY_DB_PATH}" 'SELECT turn_id FROM turns WHERE session_id = ?1 AND user_input = ?2 ORDER BY created_at ASC LIMIT 1;' "$MEMORY_SESSION_ID" "$MEMORY_FIRST_REQUEST")
  assert_non_empty "$MEMORY_FIRST_TURN_ID" 'first-run turn_id'

  set +e
  SECOND_RUN_JSON=$(run_dasall_cli dasall-cli run "$MEMORY_SECOND_REQUEST" --session "$MEMORY_SESSION_ID" --request-id pkg-smoke-memory-turn-002 --json --timeout-ms 120000 2>&1)
  SECOND_RUN_CODE=$?
  set -e
  [ "$SECOND_RUN_CODE" -eq 0 ] || fail "second run smoke failed: ${SECOND_RUN_JSON}"
  assert_json_contains "$SECOND_RUN_JSON" '"disposition":"completed"' 'second run smoke'
  assert_json_contains "$SECOND_RUN_JSON" '"task_completed":true' 'second run smoke'
  assert_json_contains "$SECOND_RUN_JSON" 'llm.origin=deepseek-prod/' 'second llm response payload'
  assert_json_contains "$SECOND_RUN_JSON" "$MEMORY_EXPECTED_MARKER" 'second llm same-session recall'
  printf '%s\n' "$SECOND_RUN_JSON" | grep -Eq '"(tool_name|capability_id)":"agent\.dataset"' && \
    fail 'second run smoke unexpectedly returned an agent.dataset tool payload instead of an LLM response'
  write_artifact_file 'run-second.json' "$SECOND_RUN_JSON"

  SECOND_SESSION_ID=$(json_extract_string "$SECOND_RUN_JSON" session_id)
  assert_non_empty "$SECOND_SESSION_ID" 'second run session_id'
  [ "$SECOND_SESSION_ID" = "$MEMORY_SESSION_ID" ] || fail "expected the second run to reuse session ${MEMORY_SESSION_ID}, got ${SECOND_SESSION_ID}"

  MEMORY_SECOND_TURN_ID=$(query_sqlite_scalar_with_params "${MEMORY_DB_PATH}" 'SELECT turn_id FROM turns WHERE session_id = ?1 AND user_input = ?2 ORDER BY created_at ASC LIMIT 1;' "$MEMORY_SESSION_ID" "$MEMORY_SECOND_REQUEST")
  assert_non_empty "$MEMORY_SECOND_TURN_ID" 'second-run turn_id'
  MEMORY_SESSION_TURN_COUNT=$(query_sqlite_scalar_with_params "${MEMORY_DB_PATH}" 'SELECT COUNT(*) FROM turns WHERE session_id = ?1;' "$MEMORY_SESSION_ID")
  assert_min_integer "$MEMORY_SESSION_TURN_COUNT" 2 'same-session turn count'
  MEMORY_SESSION_SUMMARY_COUNT_AFTER_SECOND=$(query_sqlite_scalar_with_params "${MEMORY_DB_PATH}" 'SELECT COUNT(*) FROM summaries WHERE session_id = ?1;' "$MEMORY_SESSION_ID")
  assert_min_integer "$MEMORY_SESSION_SUMMARY_COUNT_AFTER_SECOND" 2 'same-session summary row count'
  MEMORY_LATEST_SUMMARY_SOURCE_TURN_IDS_JSON=$(query_sqlite_scalar_with_params "${MEMORY_DB_PATH}" 'SELECT source_turn_ids_json FROM summaries WHERE session_id = ?1 ORDER BY created_at DESC LIMIT 1;' "$MEMORY_SESSION_ID")
  assert_non_empty "$MEMORY_LATEST_SUMMARY_SOURCE_TURN_IDS_JSON" 'latest summary source_turn_ids_json'
  printf '%s\n' "$MEMORY_LATEST_SUMMARY_SOURCE_TURN_IDS_JSON" | grep -Fq "$MEMORY_SECOND_TURN_ID" || \
    fail "latest summary source_turn_ids_json should reference ${MEMORY_SECOND_TURN_ID}, got ${MEMORY_LATEST_SUMMARY_SOURCE_TURN_IDS_JSON}"
  MEMORY_LATEST_SUMMARY_TEXT=$(query_sqlite_scalar_with_params "${MEMORY_DB_PATH}" 'SELECT summary_text FROM summaries WHERE session_id = ?1 ORDER BY created_at DESC LIMIT 1;' "$MEMORY_SESSION_ID")
  assert_non_empty "$MEMORY_LATEST_SUMMARY_TEXT" 'latest summary text'

  if [ -n "$PACKAGE_SMOKE_ARTIFACT_DIR" ]; then
    python3 - "$PACKAGE_SMOKE_ARTIFACT_DIR/memory-proof.json" \
      "$MEMORY_SESSION_ID" \
      "$MEMORY_EXPECTED_MARKER" \
      "$MEMORY_FIRST_TURN_ID" \
      "$MEMORY_SECOND_TURN_ID" \
      "$JOURNAL_MODE" \
      "$MEMORY_TABLE_COUNT" \
      "$MEMORY_VECTOR_TABLE_COUNT" \
      "$MEMORY_TURN_COUNT" \
      "$MEMORY_SUMMARY_COUNT" \
      "$MEMORY_SESSION_SUMMARY_COUNT_AFTER_FIRST" \
      "$MEMORY_SESSION_TURN_COUNT" \
      "$MEMORY_SESSION_SUMMARY_COUNT_AFTER_SECOND" \
      "$MEMORY_LATEST_SUMMARY_SOURCE_TURN_IDS_JSON" \
      "$MEMORY_LATEST_SUMMARY_TEXT" <<'PY'
import json
import sys

path = sys.argv[1]
data = {
    "session_id": sys.argv[2],
    "expected_marker": sys.argv[3],
    "first_turn_id": sys.argv[4],
    "second_turn_id": sys.argv[5],
    "journal_mode": sys.argv[6],
    "core_table_count": int(sys.argv[7]),
    "vector_table_count": int(sys.argv[8]),
    "llm_turn_writeback_count": int(sys.argv[9]),
    "llm_summary_writeback_count": int(sys.argv[10]),
    "session_summary_count_after_first": int(sys.argv[11]),
    "session_turn_count_after_second": int(sys.argv[12]),
    "session_summary_count_after_second": int(sys.argv[13]),
    "latest_summary_source_turn_ids_json": sys.argv[14],
    "latest_summary_text_prefix": sys.argv[15][:160],
}
with open(path, 'w', encoding='ascii') as handle:
    json.dump(data, handle, indent=2)
    handle.write('\n')
PY
  fi

  run_root test -x "${MEMORY_MAINTENANCE_PROOF_TOOL}"
  MAINTENANCE_PROFILE_ID=$(run_root_sh '. /etc/default/dasall-daemon && : "${DASALL_DAEMON_PROFILE_ID:?missing DASALL_DAEMON_PROFILE_ID}" && printf "%s" "${DASALL_DAEMON_PROFILE_ID}"')
  MAINTENANCE_PROOF_JSON=$(run_root "${MEMORY_MAINTENANCE_PROOF_TOOL}" \
    --profile-id "${MAINTENANCE_PROFILE_ID}" \
    --config-file /etc/dasall/daemon.json \
    --json)
  assert_json_matches "$MAINTENANCE_PROOF_JSON" '"ok": true' 'memory maintenance proof'
  assert_json_matches "$MAINTENANCE_PROOF_JSON" '"checkpoint_executed": true' 'memory maintenance checkpoint'
  assert_json_matches "$MAINTENANCE_PROOF_JSON" '"checkpoint_wal_pages_remaining": 0' 'memory maintenance checkpoint wal drain'
  assert_json_matches "$MAINTENANCE_PROOF_JSON" '"protected_turn_retained": true' 'memory maintenance protected turn retention'
  assert_json_matches "$MAINTENANCE_PROOF_JSON" '"purged_turn_removed": true' 'memory maintenance purged turn removal'
  assert_json_matches "$MAINTENANCE_PROOF_JSON" '"newest_turn_retained": true' 'memory maintenance newest turn retention'
  assert_json_matches "$MAINTENANCE_PROOF_JSON" '"quarantine_rows_after": 0' 'memory maintenance quarantine cleanup'
  assert_json_matches "$MAINTENANCE_PROOF_JSON" '"turns_before": [0-9]+' 'memory maintenance seeded turn count'
  assert_json_matches "$MAINTENANCE_PROOF_JSON" '"turns_after": [0-9]+' 'memory maintenance retained turn count'
  write_artifact_file 'memory-maintenance-proof.json' "$MAINTENANCE_PROOF_JSON"

  TOOLS_PROOF_JSON=$(run_dasall_cli "${TOOLS_INSTALLED_PROOF_TOOL}" \
    --profile-id "${MAINTENANCE_PROFILE_ID}" \
    --config-file /etc/dasall/daemon.json \
    --json)
  assert_json_matches "$TOOLS_PROOF_JSON" '"ok": true' 'tools installed proof'
  assert_json_matches "$TOOLS_PROOF_JSON" '"agent_dataset_visible": true' 'tools visible surface proof'
  assert_json_matches "$TOOLS_PROOF_JSON" '"agent_terminal_visible": true' 'tools terminal visible surface proof'
  assert_json_matches "$TOOLS_PROOF_JSON" '"tool_invocation_succeeded": true' 'tools invocation proof'
  assert_json_matches "$TOOLS_PROOF_JSON" '"terminal_confirmation_denied": true' 'tools terminal confirmation gate proof'
  assert_json_matches "$TOOLS_PROOF_JSON" '"terminal_invocation_succeeded": true' 'tools terminal invocation proof'
  assert_json_matches "$TOOLS_PROOF_JSON" '"projection_present": true' 'tools observation projection proof'
  assert_json_matches "$TOOLS_PROOF_JSON" '"terminal_projection_present": true' 'tools terminal observation projection proof'
  assert_json_matches "$TOOLS_PROOF_JSON" '"route_kind": "builtin"' 'tools builtin route proof'
  assert_json_matches "$TOOLS_PROOF_JSON" '"terminal_route_kind": "builtin"' 'tools terminal builtin route proof'
  assert_json_matches "$TOOLS_PROOF_JSON" '"route_citation_present": true' 'tools route citation proof'
  assert_json_matches "$TOOLS_PROOF_JSON" '"tool_call_citation_present": true' 'tools tool-call citation proof'
  assert_json_matches "$TOOLS_PROOF_JSON" '"production_bridge_evidence_present": true' 'tools production bridge evidence'
  assert_json_matches "$TOOLS_PROOF_JSON" '"production_observability_evidence_present": true' 'tools production observability evidence'
  assert_json_contains "$TOOLS_PROOF_JSON" '\"capability_id\":\"agent.dataset\"' 'tools payload capability marker'
  assert_json_contains "$TOOLS_PROOF_JSON" '\"projection\":\"default\"' 'tools payload projection marker'
  assert_json_contains "$TOOLS_PROOF_JSON" '\"operation\":\"agent.terminal\"' 'tools terminal payload operation marker'
  write_artifact_file 'tools-installed-proof.json' "$TOOLS_PROOF_JSON"

  RUNTIME_PROOF_JSON=$(run_dasall_cli "${RUNTIME_INSTALLED_PROOF_TOOL}" \
    --profile-id "${MAINTENANCE_PROFILE_ID}" \
    --config-file /etc/dasall/daemon.json \
    --json)
  assert_json_matches "$RUNTIME_PROOF_JSON" '"ok": true' 'runtime installed proof'
  assert_json_matches "$RUNTIME_PROOF_JSON" '"tool_status": "Completed"' 'runtime tool-positive status'
  assert_json_matches "$RUNTIME_PROOF_JSON" '"tool_task_completed": true' 'runtime tool-positive completion'
  assert_json_matches "$RUNTIME_PROOF_JSON" '"tool_runtime_path": "runtime_path:tool_positive"' 'runtime tool-positive path'
  assert_json_matches "$RUNTIME_PROOF_JSON" '"waiting_status": "PartiallyCompleted"' 'runtime waiting checkpoint proof'
  assert_json_matches "$RUNTIME_PROOF_JSON" '"recovery_positive_status": "Completed"' 'runtime recovery-positive status'
  assert_json_matches "$RUNTIME_PROOF_JSON" '"recovery_positive_task_completed": true' 'runtime recovery-positive completion'
  assert_json_matches "$RUNTIME_PROOF_JSON" '"recovery_positive_runtime_path": "runtime_path:recovery_positive"' 'runtime recovery-positive path'
  assert_json_matches "$RUNTIME_PROOF_JSON" '"recovery_negative_status": "Failed"' 'runtime recovery-negative status'
  assert_json_matches "$RUNTIME_PROOF_JSON" '"recovery_negative_binding_rejected": true' 'runtime recovery-negative binding reject'
  write_artifact_file 'runtime-installed-proof.json' "$RUNTIME_PROOF_JSON"
  if [ -n "$PACKAGE_SMOKE_ARTIFACT_DIR" ]; then
    sed 's/^      //' <<'PY' | python3 - "$PACKAGE_SMOKE_ARTIFACT_DIR"
      import json
      import pathlib
      import sys

      artifact_dir = pathlib.Path(sys.argv[1])
      run_first = json.loads((artifact_dir / 'run-first.json').read_text(encoding='utf-8'))
      runtime_installed = json.loads(
          (artifact_dir / 'runtime-installed-proof.json').read_text(encoding='utf-8'))

      runtime_summary = {
          'effective_profile_id': runtime_installed.get('effective_profile_id'),
          'direct_llm_disposition': run_first.get('disposition'),
          'direct_llm_task_completed': run_first.get('task_completed'),
          'direct_llm_llm_origin_present':
              'llm.origin=deepseek-prod/' in json.dumps(run_first, ensure_ascii=True),
          'tool_positive_runtime_path': runtime_installed.get('tool_runtime_path'),
          'tool_positive_task_completed': runtime_installed.get('tool_task_completed'),
          'waiting_status': runtime_installed.get('waiting_status'),
          'recovery_positive_runtime_path':
              runtime_installed.get('recovery_positive_runtime_path'),
          'recovery_positive_task_completed':
              runtime_installed.get('recovery_positive_task_completed'),
          'recovery_negative_status': runtime_installed.get('recovery_negative_status'),
          'recovery_negative_binding_rejected':
              runtime_installed.get('recovery_negative_binding_rejected'),
      }

      (artifact_dir / 'runtime-proof.json').write_text(
          json.dumps(runtime_summary, indent=2) + '\n',
          encoding='ascii')
PY
  fi
  if [ -n "$PACKAGE_SMOKE_ARTIFACT_DIR" ]; then
    bash "$SERVICES_INSTALLED_PROOF_SCRIPT" \
      --artifact-dir "$PACKAGE_SMOKE_ARTIFACT_DIR" \
      --tool-proof-json "$PACKAGE_SMOKE_ARTIFACT_DIR/tools-installed-proof.json"
  fi
  write_secret_consumer_package_proof
  verify_installed_async_receipt_flow

  set +e
  STATUS_JSON=$(run_root dasall-cli status receipt:missing token local://uid/0 --json 2>&1)
  STATUS_CODE=$?
  set -e
  [ "$STATUS_CODE" -eq 5 ] || fail "status missing receipt should exit 5, got ${STATUS_CODE}: ${STATUS_JSON}"
  assert_json_contains "$STATUS_JSON" '"error_ref":"status_missing"' 'status missing receipt'

  set +e
  CANCEL_JSON=$(run_root dasall-cli cancel receipt:missing token local://uid/0 --json 2>&1)
  CANCEL_CODE=$?
  set -e
  [ "$CANCEL_CODE" -eq 5 ] || fail "cancel missing receipt should exit 5, got ${CANCEL_CODE}: ${CANCEL_JSON}"
  assert_json_contains "$CANCEL_JSON" '"error_ref":"cancel_missing"' 'cancel missing receipt'

  set +e
  DIAG_JSON=$(run_root dasall-cli diag health --json 2>&1)
  DIAG_CODE=$?
  set -e
  [ "$DIAG_CODE" -eq 4 ] || fail "diag disabled should exit 4, got ${DIAG_CODE}: ${DIAG_JSON}"
  assert_json_contains "$DIAG_JSON" '"error_ref":"diag_disabled"' 'diag disabled gate'

  KNOWLEDGE_REFRESH_JSON=$(run_root dasall-cli knowledge refresh --json --timeout-ms 30000)
  assert_json_contains "$KNOWLEDGE_REFRESH_JSON" '"disposition":"completed"' 'knowledge refresh smoke'
  assert_json_contains "$KNOWLEDGE_REFRESH_JSON" '\"operation\":\"refresh\"' 'knowledge refresh payload'
  assert_json_contains "$KNOWLEDGE_REFRESH_JSON" '\"status\":\"accepted\"' 'knowledge refresh status'
  write_artifact_file 'knowledge-refresh.json' "$KNOWLEDGE_REFRESH_JSON"

  KNOWLEDGE_HEALTH_READY_JSON=$(wait_for_knowledge_refresh_ready)
  assert_knowledge_health_ready "$KNOWLEDGE_HEALTH_READY_JSON" 'knowledge health readiness smoke'
  write_artifact_file 'knowledge-health-ready.json' "$KNOWLEDGE_HEALTH_READY_JSON"

  KNOWLEDGE_RETRIEVE_JSON=$(run_root dasall-cli knowledge retrieve 'DeepSeek Chat' --json --timeout-ms 30000)
  assert_json_contains "$KNOWLEDGE_RETRIEVE_JSON" '"disposition":"completed"' 'knowledge retrieve smoke'
  assert_json_contains "$KNOWLEDGE_RETRIEVE_JSON" '\"operation\":\"retrieve\"' 'knowledge retrieve payload'
  assert_json_contains "$KNOWLEDGE_RETRIEVE_JSON" '\"ok\":true' 'knowledge retrieve ok'
  assert_json_matches "$KNOWLEDGE_RETRIEVE_JSON" '\\"slice_count\\":[1-9][0-9]*' 'knowledge retrieve slice count'
  assert_json_matches "$KNOWLEDGE_RETRIEVE_JSON" 'DeepSeek Chat|deepseek-chat|llm/providers/deepseek/' 'knowledge retrieve installed provider evidence'
  write_artifact_file 'knowledge-retrieve-provider.json' "$KNOWLEDGE_RETRIEVE_JSON"

  KNOWLEDGE_NORMATIVE_JSON=$(run_root dasall-cli knowledge retrieve 'BusinessChainIntegrationMatrix' --json --timeout-ms 30000)
  assert_json_contains "$KNOWLEDGE_NORMATIVE_JSON" '"disposition":"completed"' 'knowledge normative retrieve smoke'
  assert_json_contains "$KNOWLEDGE_NORMATIVE_JSON" '\"operation\":\"retrieve\"' 'knowledge normative retrieve payload'
  assert_json_contains "$KNOWLEDGE_NORMATIVE_JSON" '\"ok\":true' 'knowledge normative retrieve ok'
  assert_json_matches "$KNOWLEDGE_NORMATIVE_JSON" '\\"slice_count\\":[1-9][0-9]*' 'knowledge normative retrieve slice count'
  assert_json_matches "$KNOWLEDGE_NORMATIVE_JSON" 'BusinessChainIntegrationMatrix|docs/ssot/BusinessChainIntegrationMatrix.md' 'knowledge normative retrieve installed ssot evidence'
  write_artifact_file 'knowledge-retrieve-normative.json' "$KNOWLEDGE_NORMATIVE_JSON"

  KNOWLEDGE_HEALTH_FINAL_JSON=$(run_root dasall-cli knowledge health --json --timeout-ms 30000)
  assert_knowledge_health_ready "$KNOWLEDGE_HEALTH_FINAL_JSON" 'knowledge health smoke'
  write_artifact_file 'knowledge-health-final.json' "$KNOWLEDGE_HEALTH_FINAL_JSON"

  if [ -n "$PACKAGE_SMOKE_ARTIFACT_DIR" ]; then
    sed 's/^  //' <<'PY' | python3 - "$PACKAGE_SMOKE_ARTIFACT_DIR"
  import json
  import pathlib
  import re
  import sys

  artifact_dir = pathlib.Path(sys.argv[1])


  def read_text(name: str) -> str:
    return (artifact_dir / name).read_text(encoding='utf-8')


  def extract(pattern: str, text: str, label: str) -> str:
    match = re.search(pattern, text)
    if not match:
      raise SystemExit(f'missing {label} in knowledge package smoke artifact')
    return match.group(1)


  def optional_extract(pattern: str, text: str):
    match = re.search(pattern, text)
    if not match:
      return None
    return match.group(1)


  refresh_text = read_text('knowledge-refresh.json')
  health_ready_text = read_text('knowledge-health-ready.json')
  provider_text = read_text('knowledge-retrieve-provider.json')
  normative_text = read_text('knowledge-retrieve-normative.json')
  health_final_text = read_text('knowledge-health-final.json')

  data = {
    'refresh_disposition': extract(r'"disposition":"([^"]+)"', refresh_text, 'refresh disposition'),
    'refresh_status': extract(r'\\"status\\":\\"([^\\]+)\\"', refresh_text, 'refresh status'),
    'health_ready_signal': 'async_terminal' if optional_extract(r'\\"last_refresh_status\\":\\"([^\\]+)\\"', health_ready_text) else 'snapshot_freshness',
    'health_ready_state': extract(r'\\"state\\":\\"([^\\]+)\\"', health_ready_text, 'health ready state'),
    'health_ready_freshness_state': extract(r'\\"freshness_state\\":\\"([^\\]+)\\"', health_ready_text, 'health ready freshness_state'),
    'health_ready_last_refresh_status': optional_extract(r'\\"last_refresh_status\\":\\"([^\\]+)\\"', health_ready_text),
    'health_ready_active_snapshot_id': extract(r'\\"active_snapshot_id\\":\\"([^\\]+)\\"', health_ready_text, 'health ready active_snapshot_id'),
    'provider_query': 'DeepSeek Chat',
    'provider_slice_count': int(extract(r'\\"slice_count\\":([0-9]+)', provider_text, 'provider slice_count')),
    'provider_has_installed_deepseek_evidence': bool(re.search(r'DeepSeek Chat|deepseek-chat|llm/providers/deepseek/', provider_text)),
    'normative_query': 'BusinessChainIntegrationMatrix',
    'normative_slice_count': int(extract(r'\\"slice_count\\":([0-9]+)', normative_text, 'normative slice count')),
    'normative_has_ssot_evidence': bool(re.search(r'BusinessChainIntegrationMatrix|docs/ssot/BusinessChainIntegrationMatrix\.md', normative_text)),
    'health_final_state': extract(r'\\"state\\":\\"([^\\]+)\\"', health_final_text, 'health final state'),
    'health_final_freshness_state': extract(r'\\"freshness_state\\":\\"([^\\]+)\\"', health_final_text, 'health final freshness_state'),
    'health_final_active_snapshot_id': extract(r'\\"active_snapshot_id\\":\\"([^\\]+)\\"', health_final_text, 'health final active_snapshot_id'),
    'health_final_refresh_in_flight': (
      optional_extract(r'\\"refresh_in_flight\\":(true|false)', health_final_text) == 'true'
      if optional_extract(r'\\"refresh_in_flight\\":(true|false)', health_final_text) is not None
      else None
    ),
  }

  (artifact_dir / 'knowledge-proof.json').write_text(
    json.dumps(data, indent=2) + '\n',
    encoding='ascii',
  )
PY

    capture_root_file_to_artifact 'logging-main-runtime.log' "$INSTALLED_RUNTIME_LOG_PATH"
    capture_root_file_to_artifact 'logging-runtime-tool-positive.log' "$RUNTIME_TOOL_POSITIVE_LOG_PATH"
    capture_root_file_to_artifact 'logging-runtime-recovery-positive.log' "$RUNTIME_RECOVERY_POSITIVE_LOG_PATH"
    capture_root_file_to_artifact 'logging-runtime-recovery-negative.log' "$RUNTIME_RECOVERY_NEGATIVE_LOG_PATH"
    python3 "${SCRIPT_DIR}/generate_logging_package_proof.py" \
      "$PACKAGE_SMOKE_ARTIFACT_DIR" \
      "$LOGGING_PROOF_START_TS_MS"
  fi

  run_root test -f /usr/share/dasall/docs/architecture/DASALL_Engineering_Blueprint.md
  run_root test -f /usr/share/dasall/docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md
  run_root test -f /usr/share/dasall/docs/ssot/BusinessChainIntegrationMatrix.md
  run_root test -f /usr/share/dasall/llm/prompts/planner/default/manifest.yaml
  run_root test -f /usr/share/dasall/llm/prompts/responder/default/manifest.yaml
  run_root test -f /usr/share/dasall/llm/providers/catalog.yaml
  run_root test -f /usr/share/dasall/llm/providers/deepseek/manifest.yaml
  run_root dasall-cli version >/dev/null
}

usage() {
  cat <<'EOF'
Usage: bash scripts/packaging/pkg_smoke_install.sh [--explicit-start-check] [--tui-daemon-backed-check]
EOF
}

EXPLICIT_START_CHECK=0
TUI_DAEMON_BACKED_CHECK=0

while [ "$#" -gt 0 ]; do
  case "$1" in
    --explicit-start-check)
      EXPLICIT_START_CHECK=1
      ;;
    --tui-daemon-backed-check)
      TUI_DAEMON_BACKED_CHECK=1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      usage
      fail "unknown option: ${1}"
      ;;
  esac
  shift
done

require_command dpkg
require_command dpkg-parsechangelog
require_command systemctl
require_artifact "$COMMON_DEB"
require_artifact "$CLI_DEB"
require_artifact "$DAEMON_DEB"
require_artifact "$META_DEB"

preserve_existing_llm_secret
reset_existing_state
install_packages
verify_packages_installed
verify_service_not_started
verify_installed_files
verify_validate_only

if [ "$EXPLICIT_START_CHECK" -eq 1 ]; then
  verify_explicit_start
elif [ "$TUI_DAEMON_BACKED_CHECK" -eq 1 ]; then
  verify_tui_daemon_backed_check
fi

log 'install smoke passed'