#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "${SCRIPT_DIR}/../.." && pwd)
ARTIFACT_DIR=$(CDPATH= cd -- "${REPO_ROOT}/.." && pwd)
ARCH=$(dpkg --print-architecture)
VERSION=$(cd "${REPO_ROOT}" && dpkg-parsechangelog -SVersion)

COMMON_DEB="${ARTIFACT_DIR}/dasall-common_${VERSION}_all.deb"
CLI_DEB="${ARTIFACT_DIR}/dasall-cli_${VERSION}_${ARCH}.deb"
DAEMON_DEB="${ARTIFACT_DIR}/dasall-daemon_${VERSION}_${ARCH}.deb"
META_DEB="${ARTIFACT_DIR}/dasall_${VERSION}_all.deb"
LLM_SECRET_PATH=/var/lib/dasall/secrets/llm/providers/deepseek-prod.secret
MEMORY_DB_PATH=/var/lib/dasall/memory/memory.db
PRESERVED_SECRET_ROOT=

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

assert_positive_integer() {
  value=$1
  label=$2
  case "$value" in
    ''|*[!0-9]*) fail "${label}: expected a non-negative integer, got ${value}" ;;
  esac
  [ "$value" -ge 1 ] || fail "${label}: expected at least one row, got ${value}"
}

cleanup() {
  run_root systemctl disable --now dasall-daemon.service >/dev/null 2>&1 || true
  if [ -n "${PRESERVED_SECRET_ROOT}" ]; then
    run_root rm -rf "${PRESERVED_SECRET_ROOT}" >/dev/null 2>&1 || true
  fi
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
  else
    run_root test -f "${PRESERVED_SECRET_ROOT}/deepseek-prod.key"
    run_root_sh "
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
      dasall config apply --from-file '${PRESERVED_SECRET_ROOT}/desired.yaml' --no-input --json >/dev/null
    "
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
}

verify_validate_only() {
  log 'verifying daemon validate-only against installed defaults'
  run_root_sh '. /etc/default/dasall-daemon && : "${DASALL_DAEMON_PROFILE_ID:?missing DASALL_DAEMON_PROFILE_ID}" && /usr/sbin/dasall-daemon --validate-only --profile-id="${DASALL_DAEMON_PROFILE_ID}" --config-file /etc/dasall/daemon.json'
}

verify_explicit_start() {
  log 'verifying daemon stays stopped until explicit service enable/start'
  run_root_sh '! systemctl is-enabled --quiet dasall-daemon.service >/dev/null 2>&1'
  run_root_sh '! systemctl is-active --quiet dasall-daemon.service >/dev/null 2>&1'

  restore_preserved_llm_secret

  run_root systemctl enable --now dasall-daemon.service

  run_root systemctl is-active --quiet dasall-daemon.service
  run_root systemctl is-enabled --quiet dasall-daemon.service
  run_root test -S /run/dasall/daemon.sock
  run_root_sh 'test "$(stat -c "%U:%G" /run/dasall/daemon.sock)" = "dasall:dasall"'
  run_root_sh 'test "$(stat -c "%a" /run/dasall/daemon.sock)" = "600"'
  run_dasall_cli dasall ping --json >/dev/null
  run_dasall_cli dasall readiness --json >/dev/null
  RUN_JSON=$(run_dasall_cli dasall run '{"prompt":"package smoke"}' --json --timeout-ms 120000)
  assert_json_contains "$RUN_JSON" '"disposition":"completed"' 'run smoke'
  assert_json_contains "$RUN_JSON" '"task_completed":true' 'run smoke'
  assert_json_contains "$RUN_JSON" 'llm.origin=deepseek-prod/' 'llm response payload'
  printf '%s\n' "$RUN_JSON" | grep -Fq 'agent.dataset' && \
    fail 'run smoke unexpectedly returned agent.dataset payload instead of LLM response'

  require_command python3
  run_root test -f /usr/share/dasall/sql/memory/V001__initial_schema.sql
  run_root test -f "${MEMORY_DB_PATH}"
  JOURNAL_MODE=$(query_sqlite_scalar "${MEMORY_DB_PATH}" 'PRAGMA journal_mode;')
  [ "$JOURNAL_MODE" = "wal" ] || fail "memory sqlite journal mode should be wal, got ${JOURNAL_MODE}"
  MEMORY_TABLE_COUNT=$(query_sqlite_scalar "${MEMORY_DB_PATH}" "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name IN ('sessions','turns','summaries','facts','experiences');")
  [ "$MEMORY_TABLE_COUNT" -eq 5 ] || fail "memory sqlite schema should expose five core tables, got ${MEMORY_TABLE_COUNT}"
  MEMORY_TURN_COUNT=$(query_sqlite_scalar "${MEMORY_DB_PATH}" "SELECT COUNT(*) FROM turns WHERE user_input LIKE '%package smoke%' AND agent_response LIKE 'llm.origin=deepseek-prod/%';")
  assert_positive_integer "$MEMORY_TURN_COUNT" 'memory sqlite llm turn writeback'
  MEMORY_SUMMARY_COUNT=$(query_sqlite_scalar "${MEMORY_DB_PATH}" "SELECT COUNT(*) FROM summaries WHERE summary_text LIKE 'llm.origin=deepseek-prod/%';")
  assert_positive_integer "$MEMORY_SUMMARY_COUNT" 'memory sqlite llm summary writeback'

  set +e
  STATUS_JSON=$(run_root dasall status receipt:missing token local://uid/0 --json 2>&1)
  STATUS_CODE=$?
  set -e
  [ "$STATUS_CODE" -eq 5 ] || fail "status missing receipt should exit 5, got ${STATUS_CODE}: ${STATUS_JSON}"
  assert_json_contains "$STATUS_JSON" '"error_ref":"status_missing"' 'status missing receipt'

  set +e
  CANCEL_JSON=$(run_root dasall cancel receipt:missing token local://uid/0 --json 2>&1)
  CANCEL_CODE=$?
  set -e
  [ "$CANCEL_CODE" -eq 5 ] || fail "cancel missing receipt should exit 5, got ${CANCEL_CODE}: ${CANCEL_JSON}"
  assert_json_contains "$CANCEL_JSON" '"error_ref":"cancel_missing"' 'cancel missing receipt'

  set +e
  DIAG_JSON=$(run_root dasall diag health --json 2>&1)
  DIAG_CODE=$?
  set -e
  [ "$DIAG_CODE" -eq 4 ] || fail "diag disabled should exit 4, got ${DIAG_CODE}: ${DIAG_JSON}"
  assert_json_contains "$DIAG_JSON" '"error_ref":"diag_disabled"' 'diag disabled gate'

  KNOWLEDGE_REFRESH_JSON=$(run_root dasall knowledge refresh --json --timeout-ms 30000)
  assert_json_contains "$KNOWLEDGE_REFRESH_JSON" '"disposition":"completed"' 'knowledge refresh smoke'
  assert_json_contains "$KNOWLEDGE_REFRESH_JSON" '\"operation\":\"refresh\"' 'knowledge refresh payload'
  assert_json_contains "$KNOWLEDGE_REFRESH_JSON" '\"status\":\"accepted\"' 'knowledge refresh status'

  KNOWLEDGE_RETRIEVE_JSON=$(run_root dasall knowledge retrieve 'DeepSeek Chat' --json --timeout-ms 30000)
  assert_json_contains "$KNOWLEDGE_RETRIEVE_JSON" '"disposition":"completed"' 'knowledge retrieve smoke'
  assert_json_contains "$KNOWLEDGE_RETRIEVE_JSON" '\"operation\":\"retrieve\"' 'knowledge retrieve payload'
  assert_json_contains "$KNOWLEDGE_RETRIEVE_JSON" '\"ok\":true' 'knowledge retrieve ok'
  assert_json_matches "$KNOWLEDGE_RETRIEVE_JSON" '\\"slice_count\\":[1-9][0-9]*' 'knowledge retrieve slice count'
  assert_json_matches "$KNOWLEDGE_RETRIEVE_JSON" 'DeepSeek Chat|deepseek-chat|llm/providers/deepseek/' 'knowledge retrieve installed provider evidence'

  KNOWLEDGE_HEALTH_JSON=$(run_root dasall knowledge health --json --timeout-ms 30000)
  assert_json_contains "$KNOWLEDGE_HEALTH_JSON" '"disposition":"completed"' 'knowledge health smoke'
  assert_json_contains "$KNOWLEDGE_HEALTH_JSON" '\"operation\":\"health\"' 'knowledge health payload'
  assert_json_contains "$KNOWLEDGE_HEALTH_JSON" '\"active_snapshot_id\":\"snapshot:' 'knowledge health active snapshot'

  run_root test -f /usr/share/dasall/llm/prompts/planner/default/manifest.yaml
  run_root test -f /usr/share/dasall/llm/prompts/responder/default/manifest.yaml
  run_root test -f /usr/share/dasall/llm/providers/catalog.yaml
  run_root test -f /usr/share/dasall/llm/providers/deepseek/manifest.yaml
  run_root dasall version >/dev/null
}

usage() {
  cat <<'EOF'
Usage: bash scripts/packaging/pkg_smoke_install.sh [--explicit-start-check]
EOF
}

EXPLICIT_START_CHECK=0

case "${1:-}" in
  "")
    ;;
  --explicit-start-check)
    EXPLICIT_START_CHECK=1
    shift
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

[ "$#" -eq 0 ] || fail 'unexpected positional arguments'

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
fi

log 'install smoke passed'