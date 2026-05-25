#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "${SCRIPT_DIR}/../.." && pwd)
ARTIFACT_ROOT=$(CDPATH= cd -- "${REPO_ROOT}/.." && pwd)
ARCH=$(dpkg --print-architecture)
VERSION=$(cd "${REPO_ROOT}" && dpkg-parsechangelog -SVersion)

COMMON_DEB="${ARTIFACT_ROOT}/dasall-common_${VERSION}_all.deb"
CLI_DEB="${ARTIFACT_ROOT}/dasall-cli_${VERSION}_${ARCH}.deb"
DAEMON_DEB="${ARTIFACT_ROOT}/dasall-daemon_${VERSION}_${ARCH}.deb"
META_DEB="${ARTIFACT_ROOT}/dasall_${VERSION}_all.deb"
ARTIFACT_DIR=${DASALL_KNOWLEDGE_FAILURE_ARTIFACT_DIR:-/tmp/dasall-knowledge-failure-proof}
TIMEOUT_MS=${DASALL_KNOWLEDGE_FAILURE_TIMEOUT_MS:-30000}
DAEMON_READY_ATTEMPTS=${DASALL_KNOWLEDGE_FAILURE_DAEMON_READY_ATTEMPTS:-90}
KNOWLEDGE_READY_ATTEMPTS=${DASALL_KNOWLEDGE_FAILURE_KNOWLEDGE_READY_ATTEMPTS:-60}
PROVIDER_QUERY=${DASALL_KNOWLEDGE_FAILURE_PROVIDER_QUERY:-DeepSeek Chat}
STATE_ROOT=${DASALL_KNOWLEDGE_FAILURE_STATE_ROOT:-/var/lib/dasall}

log() {
  printf '[knowledge-failure-proof] %s\n' "$*"
}

fail() {
  printf '[knowledge-failure-proof] %s\n' "$*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Usage: bash scripts/packaging/knowledge_failure_injection_installed_proof.sh [--artifact-dir <path>] [--timeout-ms <value>]
EOF
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
  sudo -n true >/dev/null 2>&1 || fail 'passwordless sudo is required for knowledge failure proof; rerun as root'
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

write_artifact_file() {
  file_name=$1
  file_content=$2

  ensure_artifact_dir
  printf '%s\n' "$file_content" > "$ARTIFACT_DIR/$file_name"
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

json_contains_fragment() {
  json_payload=$1
  expected=$2

  printf '%s\n' "$json_payload" | grep -Fq "$expected"
}

readiness_is_ready() {
  json_payload=$1

  json_contains_fragment "$json_payload" '"disposition":"completed"' || return 1
  json_contains_fragment "$json_payload" '\"state\":\"READY\"'
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

extract_escaped_value() {
  json_payload=$1
  key=$2

  value=$(printf '%s\n' "$json_payload" | sed -n "s/.*\\\\\"${key}\\\\\":\\\\\"\([^\\\\\"]*\)\\\\\".*/\1/p" | head -n 1)
  [ -n "$value" ] || fail "missing escaped field ${key} in payload: ${json_payload}"
  printf '%s\n' "$value"
}

extract_slice_count() {
  json_payload=$1

  value=$(printf '%s\n' "$json_payload" | sed -n 's/.*\\"slice_count\\":\([0-9][0-9]*\).*/\1/p' | head -n 1)
  [ -n "$value" ] || fail "missing slice_count in payload: ${json_payload}"
  printf '%s\n' "$value"
}

capture_journal() {
  file_name=$1
  if ! command -v journalctl >/dev/null 2>&1; then
    return 0
  fi

  ensure_artifact_dir
  if [ "$(id -u)" -eq 0 ]; then
    journalctl -u dasall-daemon --since '10 minutes ago' --no-pager > "$ARTIFACT_DIR/$file_name" 2>&1 || true
  else
    sudo journalctl -u dasall-daemon --since '10 minutes ago' --no-pager > "$ARTIFACT_DIR/$file_name" 2>&1 || true
  fi
}

wait_for_daemon_ready() {
  attempts=0
  while [ "$attempts" -lt "$DAEMON_READY_ATTEMPTS" ]; do
    if run_root_sh 'systemctl is-enabled --quiet dasall-daemon.service >/dev/null 2>&1 &&
       systemctl is-active --quiet dasall-daemon.service >/dev/null 2>&1'; then
      if readiness_json=$(run_root dasall-cli readiness --json --timeout-ms "$TIMEOUT_MS" 2>/dev/null); then
        if readiness_is_ready "$readiness_json"; then
          printf '%s\n' "$readiness_json"
          return 0
        fi
      fi
    fi

    attempts=$((attempts + 1))
    sleep 1
  done

  run_root systemctl status --no-pager dasall-daemon.service >&2 || true
  fail 'timed out waiting for daemon ready state'
}

wait_for_knowledge_ready() {
  attempts=0
  while [ "$attempts" -lt "$KNOWLEDGE_READY_ATTEMPTS" ]; do
    if health_json=$(run_root dasall-cli knowledge health --json --timeout-ms "$TIMEOUT_MS" 2>/dev/null); then
      if knowledge_health_is_ready "$health_json"; then
        printf '%s\n' "$health_json"
        return 0
      fi
      if json_contains_fragment "$health_json" '"last_refresh_status":"failed"'; then
        fail "knowledge refresh terminal failure: ${health_json}"
      fi
    fi
    attempts=$((attempts + 1))
    sleep 1
  done

  fail 'timed out waiting for knowledge ready state'
}

wait_for_knowledge_snapshot_rotation() {
  previous_snapshot_id=$1
  attempts=0
  while [ "$attempts" -lt "$KNOWLEDGE_READY_ATTEMPTS" ]; do
    if health_json=$(run_root dasall-cli knowledge health --json --timeout-ms "$TIMEOUT_MS" 2>/dev/null); then
      if knowledge_health_is_ready "$health_json"; then
        current_snapshot_id=$(extract_escaped_value "$health_json" active_snapshot_id)
        if [ "$current_snapshot_id" != "$previous_snapshot_id" ]; then
          printf '%s\n' "$health_json"
          return 0
        fi
      fi
      if json_contains_fragment "$health_json" '"last_refresh_status":"failed"'; then
        fail "knowledge refresh terminal failure: ${health_json}"
      fi
    fi
    attempts=$((attempts + 1))
    sleep 1
  done

  fail "timed out waiting for knowledge snapshot rotation from ${previous_snapshot_id}"
}

reset_existing_state() {
  log 'resetting previous installed state'
  run_root_sh '
    systemctl disable --now dasall-daemon.service >/dev/null 2>&1 || true
    dpkg -P dasall dasall-daemon dasall-cli dasall-common >/dev/null 2>&1 || true
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

cleanup() {
  run_root systemctl disable --now dasall-daemon.service >/dev/null 2>&1 || true
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --artifact-dir)
      [ "$#" -ge 2 ] || fail 'missing value for --artifact-dir'
      ARTIFACT_DIR=$2
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

ensure_positive_integer "$TIMEOUT_MS" 'timeout-ms'
ensure_positive_integer "$DAEMON_READY_ATTEMPTS" 'daemon-ready-attempts'
ensure_positive_integer "$KNOWLEDGE_READY_ATTEMPTS" 'knowledge-ready-attempts'
require_command dpkg
require_command dpkg-parsechangelog
require_command systemctl
require_command python3
require_artifact "$COMMON_DEB"
require_artifact "$CLI_DEB"
require_artifact "$DAEMON_DEB"
require_artifact "$META_DEB"
ensure_artifact_dir

trap cleanup EXIT INT TERM

reset_existing_state
install_packages
verify_packages_installed
verify_service_not_started

run_root systemctl enable --now dasall-daemon.service
READY_BEFORE_JSON=$(wait_for_daemon_ready)
write_artifact_file 'ready-before.json' "$READY_BEFORE_JSON"

STARTUP_HEALTH_JSON=$(wait_for_knowledge_ready)
assert_knowledge_health_ready "$STARTUP_HEALTH_JSON" 'knowledge health after startup prewarm'
write_artifact_file 'knowledge-health-after-startup.json' "$STARTUP_HEALTH_JSON"
STARTUP_SNAPSHOT_ID=$(extract_escaped_value "$STARTUP_HEALTH_JSON" active_snapshot_id)

INITIAL_REFRESH_JSON=$(run_root dasall-cli knowledge refresh --json --timeout-ms "$TIMEOUT_MS")
assert_json_contains "$INITIAL_REFRESH_JSON" '"disposition":"completed"' 'initial knowledge refresh disposition'
assert_json_contains "$INITIAL_REFRESH_JSON" '\"operation\":\"refresh\"' 'initial knowledge refresh payload'
assert_json_contains "$INITIAL_REFRESH_JSON" '\"status\":\"accepted\"' 'initial knowledge refresh status'
write_artifact_file 'knowledge-initial-refresh.json' "$INITIAL_REFRESH_JSON"

HEALTH_BEFORE_JSON=$(wait_for_knowledge_snapshot_rotation "$STARTUP_SNAPSHOT_ID")
assert_knowledge_health_ready "$HEALTH_BEFORE_JSON" 'knowledge health before failure injection'
write_artifact_file 'knowledge-health-before.json' "$HEALTH_BEFORE_JSON"
INITIAL_SNAPSHOT_ID=$(extract_escaped_value "$HEALTH_BEFORE_JSON" active_snapshot_id)
[ "$INITIAL_SNAPSHOT_ID" != "$STARTUP_SNAPSHOT_ID" ] || \
  fail 'initial explicit refresh did not rotate the active snapshot beyond startup prewarm'

REFRESH_JSON=$(run_root dasall-cli knowledge refresh --json --timeout-ms "$TIMEOUT_MS")
assert_json_contains "$REFRESH_JSON" '"disposition":"completed"' 'knowledge refresh before corruption'
assert_json_contains "$REFRESH_JSON" '\"operation\":\"refresh\"' 'knowledge refresh payload before corruption'
assert_json_contains "$REFRESH_JSON" '\"status\":\"accepted\"' 'knowledge refresh accepted before corruption'
write_artifact_file 'knowledge-refresh-before-corruption.json' "$REFRESH_JSON"

HEALTH_AFTER_REFRESH_JSON=$(wait_for_knowledge_snapshot_rotation "$INITIAL_SNAPSHOT_ID")
assert_knowledge_health_ready "$HEALTH_AFTER_REFRESH_JSON" 'knowledge health after second refresh'
write_artifact_file 'knowledge-health-after-refresh.json' "$HEALTH_AFTER_REFRESH_JSON"
CORRUPTED_SNAPSHOT_ID=$(extract_escaped_value "$HEALTH_AFTER_REFRESH_JSON" active_snapshot_id)
[ "$CORRUPTED_SNAPSHOT_ID" != "$INITIAL_SNAPSHOT_ID" ] || \
  fail 'second refresh did not rotate the active snapshot; unable to build an LKG recovery proof'

PROVIDER_BEFORE_JSON=$(run_root dasall-cli knowledge retrieve "$PROVIDER_QUERY" --json --timeout-ms "$TIMEOUT_MS")
assert_json_contains "$PROVIDER_BEFORE_JSON" '"disposition":"completed"' 'knowledge retrieve before corruption'
assert_json_contains "$PROVIDER_BEFORE_JSON" '\"ok\":true' 'knowledge retrieve before corruption ok'
assert_json_matches "$PROVIDER_BEFORE_JSON" '\\"slice_count\\":[1-9][0-9]*' 'knowledge retrieve before corruption slice count'
write_artifact_file 'knowledge-retrieve-before-corruption.json' "$PROVIDER_BEFORE_JSON"

CORRUPTED_DB_PATH="${STATE_ROOT}/knowledge/snapshots/${CORRUPTED_SNAPSHOT_ID}/lexical.sqlite"
run_root test -f "$CORRUPTED_DB_PATH"
run_root systemctl disable --now dasall-daemon.service >/dev/null 2>&1 || true
run_root rm -f "$CORRUPTED_DB_PATH"
cat > "$ARTIFACT_DIR/corruption-target.json" <<EOF
{
  "state_root": "${STATE_ROOT}",
  "initial_snapshot_id": "${INITIAL_SNAPSHOT_ID}",
  "corrupted_snapshot_id": "${CORRUPTED_SNAPSHOT_ID}",
  "corrupted_db_path": "${CORRUPTED_DB_PATH}"
}
EOF

run_root systemctl enable --now dasall-daemon.service
READY_AFTER_JSON=$(wait_for_daemon_ready)
write_artifact_file 'ready-after-corruption.json' "$READY_AFTER_JSON"

HEALTH_AFTER_RECOVERY_JSON=$(wait_for_knowledge_ready)
assert_knowledge_health_ready "$HEALTH_AFTER_RECOVERY_JSON" 'knowledge health after corruption recovery'
assert_json_contains "$HEALTH_AFTER_RECOVERY_JSON" '\"last_known_good_available\":true' 'knowledge recovery should advertise last-known-good availability'
write_artifact_file 'knowledge-health-after-recovery.json' "$HEALTH_AFTER_RECOVERY_JSON"
RECOVERED_SNAPSHOT_ID=$(extract_escaped_value "$HEALTH_AFTER_RECOVERY_JSON" active_snapshot_id)
[ "$RECOVERED_SNAPSHOT_ID" != "$CORRUPTED_SNAPSHOT_ID" ] || \
  fail "recovered active snapshot ${RECOVERED_SNAPSHOT_ID} still matches the corrupted snapshot"

LEDGER_JSONL=$(run_root cat "${STATE_ROOT}/knowledge/version_ledger.jsonl")
printf '%s\n' "$LEDGER_JSONL" > "$ARTIFACT_DIR/version-ledger-after-recovery.jsonl"
RECOVERED_PARENT_SNAPSHOT_ID=$(python3 - "$ARTIFACT_DIR/version-ledger-after-recovery.jsonl" "$RECOVERED_SNAPSHOT_ID" <<'PY'
import json
import pathlib
import sys

ledger_path = pathlib.Path(sys.argv[1])
recovered_snapshot_id = sys.argv[2]

for line in reversed(ledger_path.read_text(encoding='utf-8').splitlines()):
    if not line.strip():
        continue
    entry = json.loads(line)
    if 'snapshot_id' not in entry:
        continue
    if entry['snapshot_id'] == recovered_snapshot_id:
        print(entry.get('parent_snapshot_id', ''))
        break
else:
    raise SystemExit(f'missing recovered snapshot entry: {recovered_snapshot_id}')
PY
)

if [ "$RECOVERED_SNAPSHOT_ID" = "$INITIAL_SNAPSHOT_ID" ]; then
  RECOVERY_MODE='restored-last-known-good'
elif [ "$RECOVERED_PARENT_SNAPSHOT_ID" = "$INITIAL_SNAPSHOT_ID" ]; then
  RECOVERY_MODE='refreshed-from-last-known-good'
else
  fail "recovered active snapshot ${RECOVERED_SNAPSHOT_ID} neither matched expected last-known-good ${INITIAL_SNAPSHOT_ID} nor descended from it"
fi

PROVIDER_AFTER_JSON=$(run_root dasall-cli knowledge retrieve "$PROVIDER_QUERY" --json --timeout-ms "$TIMEOUT_MS")
assert_json_contains "$PROVIDER_AFTER_JSON" '"disposition":"completed"' 'knowledge retrieve after corruption recovery'
assert_json_contains "$PROVIDER_AFTER_JSON" '\"ok\":true' 'knowledge retrieve after corruption recovery ok'
assert_json_matches "$PROVIDER_AFTER_JSON" '\\"slice_count\\":[1-9][0-9]*' 'knowledge retrieve after corruption recovery slice count'
assert_json_matches "$PROVIDER_AFTER_JSON" 'DeepSeek Chat|deepseek-chat|llm/providers/deepseek/' 'knowledge retrieve after corruption recovery provider evidence'
write_artifact_file 'knowledge-retrieve-after-recovery.json' "$PROVIDER_AFTER_JSON"
PROVIDER_SLICE_COUNT=$(extract_slice_count "$PROVIDER_AFTER_JSON")

CATALOG_JSON=$(run_root cat "${STATE_ROOT}/knowledge/corpus_catalog.json")
printf '%s\n' "$CATALOG_JSON" > "$ARTIFACT_DIR/corpus-catalog-after-recovery.json"
printf '%s\n' "$CATALOG_JSON" | grep -Fq "$RECOVERED_SNAPSHOT_ID" || \
  fail 'recovered corpus catalog does not contain the restored active snapshot id'
if printf '%s\n' "$CATALOG_JSON" | grep -Fq "$CORRUPTED_SNAPSHOT_ID"; then
  fail 'recovered corpus catalog still advertises the corrupted active snapshot id'
fi

capture_journal 'knowledge-failure-injection-journal.log'

python3 - "$ARTIFACT_DIR" "$INITIAL_SNAPSHOT_ID" "$CORRUPTED_SNAPSHOT_ID" "$RECOVERED_SNAPSHOT_ID" "$RECOVERED_PARENT_SNAPSHOT_ID" "$RECOVERY_MODE" "$CORRUPTED_DB_PATH" "$PROVIDER_QUERY" "$PROVIDER_SLICE_COUNT" <<'PY'
import json
import pathlib
import sys

artifact_dir = pathlib.Path(sys.argv[1])
initial_snapshot_id = sys.argv[2]
corrupted_snapshot_id = sys.argv[3]
recovered_snapshot_id = sys.argv[4]
recovered_parent_snapshot_id = sys.argv[5]
recovery_mode = sys.argv[6]
corrupted_db_path = sys.argv[7]
provider_query = sys.argv[8]
provider_slice_count = int(sys.argv[9])

summary = {
    'initial_active_snapshot_id': initial_snapshot_id,
    'corrupted_active_snapshot_id': corrupted_snapshot_id,
    'recovered_active_snapshot_id': recovered_snapshot_id,
  'recovered_parent_snapshot_id': recovered_parent_snapshot_id,
  'recovery_mode': recovery_mode,
    'corrupted_db_path': corrupted_db_path,
    'recovered_equals_initial': recovered_snapshot_id == initial_snapshot_id,
  'recovered_descends_from_initial': recovered_parent_snapshot_id == initial_snapshot_id,
    'recovered_differs_from_corrupted': recovered_snapshot_id != corrupted_snapshot_id,
    'provider_query': provider_query,
    'provider_slice_count_after_recovery': provider_slice_count,
    'catalog_aligned_to_recovered_snapshot': True,
    'corrupted_snapshot_removed_from_catalog': True,
}

(artifact_dir / 'knowledge-failure-injection-proof.json').write_text(
    json.dumps(summary, indent=2) + '\n',
    encoding='ascii',
)
PY

log 'knowledge local installed failure injection proof passed'