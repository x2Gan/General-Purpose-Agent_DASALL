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
ARTIFACT_DIR=${DASALL_KNOWLEDGE_PROOF_ARTIFACT_DIR:-/tmp/dasall-knowledge-proof}
TIMEOUT_MS=${DASALL_KNOWLEDGE_PROOF_TIMEOUT_MS:-30000}
DAEMON_READY_ATTEMPTS=${DASALL_KNOWLEDGE_PROOF_DAEMON_READY_ATTEMPTS:-90}
KNOWLEDGE_READY_ATTEMPTS=${DASALL_KNOWLEDGE_PROOF_KNOWLEDGE_READY_ATTEMPTS:-60}
PROVIDER_QUERY=${DASALL_KNOWLEDGE_PROOF_PROVIDER_QUERY:-DeepSeek Chat}

log() {
  printf '[knowledge-proof] %s\n' "$*"
}

fail() {
  printf '[knowledge-proof] %s\n' "$*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Usage: bash scripts/packaging/knowledge_local_installed_proof.sh [--artifact-dir <path>] [--timeout-ms <value>]
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
  sudo -n true >/dev/null 2>&1 || fail 'passwordless sudo is required for knowledge proof; rerun as root'
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

wait_for_daemon_ready() {
  attempt=0
  while [ "$attempt" -lt "$DAEMON_READY_ATTEMPTS" ]; do
    if run_root_sh 'systemctl is-enabled --quiet dasall-daemon.service >/dev/null 2>&1 &&
       systemctl is-active --quiet dasall-daemon.service >/dev/null 2>&1'; then
      if readiness_json=$(run_root dasall readiness --json --timeout-ms "$TIMEOUT_MS" 2>/dev/null); then
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

wait_for_knowledge_refresh_ready() {
  attempts=0
  while [ "$attempts" -lt "$KNOWLEDGE_READY_ATTEMPTS" ]; do
    if health_json=$(run_root dasall knowledge health --json --timeout-ms "$TIMEOUT_MS" 2>/dev/null); then
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

  fail 'timed out waiting for async knowledge refresh completion'
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

write_proof_summary() {
  python3 - "$ARTIFACT_DIR" "$PROVIDER_QUERY" <<'PY'
import json
import pathlib
import re
import sys

artifact_dir = pathlib.Path(sys.argv[1])
provider_query = sys.argv[2]

def read_text(name: str) -> str:
    return (artifact_dir / name).read_text(encoding='utf-8')

def extract(pattern: str, text: str, label: str) -> str:
    match = re.search(pattern, text)
    if not match:
        raise SystemExit(f'missing {label}')
    return match.group(1)

def optional_extract(pattern: str, text: str):
    match = re.search(pattern, text)
    if not match:
        return None
    return match.group(1)

ready_text = read_text('ready.json')
refresh_text = read_text('knowledge-refresh.json')
health_ready_text = read_text('knowledge-health-ready.json')
provider_text = read_text('knowledge-retrieve-provider.json')
health_final_text = read_text('knowledge-health-final.json')

data = {
    'ready_state': extract(r'\\"state\\":\\"([^\\]+)\\"', ready_text, 'ready state'),
    'ready_runtime_readiness': extract(r'\\"runtime_readiness\\":\\"([^\\]+)\\"', ready_text, 'ready runtime_readiness'),
    'refresh_disposition': extract(r'"disposition":"([^"]+)"', refresh_text, 'refresh disposition'),
    'refresh_status': extract(r'\\"status\\":\\"([^\\]+)\\"', refresh_text, 'refresh status'),
    'health_ready_signal': 'async_terminal' if optional_extract(r'\\"last_refresh_status\\":\\"([^\\]+)\\"', health_ready_text) else 'snapshot_freshness',
    'health_ready_state': extract(r'\\"state\\":\\"([^\\]+)\\"', health_ready_text, 'health ready state'),
    'health_ready_freshness_state': extract(r'\\"freshness_state\\":\\"([^\\]+)\\"', health_ready_text, 'health ready freshness_state'),
    'health_ready_last_refresh_status': optional_extract(r'\\"last_refresh_status\\":\\"([^\\]+)\\"', health_ready_text),
    'health_ready_active_snapshot_id': extract(r'\\"active_snapshot_id\\":\\"([^\\]+)\\"', health_ready_text, 'health ready active snapshot'),
    'provider_query': provider_query,
    'provider_slice_count': int(extract(r'\\"slice_count\\":([0-9]+)', provider_text, 'provider slice_count')),
    'provider_has_installed_deepseek_evidence': bool(re.search(r'DeepSeek Chat|deepseek-chat|llm/providers/deepseek/', provider_text)),
    'health_final_state': extract(r'\\"state\\":\\"([^\\]+)\\"', health_final_text, 'health final state'),
    'health_final_freshness_state': extract(r'\\"freshness_state\\":\\"([^\\]+)\\"', health_final_text, 'health final freshness_state'),
    'health_final_active_snapshot_id': extract(r'\\"active_snapshot_id\\":\\"([^\\]+)\\"', health_final_text, 'health final active snapshot'),
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
READY_JSON=$(wait_for_daemon_ready)
write_artifact_file 'ready.json' "$READY_JSON"

KNOWLEDGE_REFRESH_JSON=$(run_root dasall knowledge refresh --json --timeout-ms "$TIMEOUT_MS")
assert_json_contains "$KNOWLEDGE_REFRESH_JSON" '"disposition":"completed"' 'knowledge refresh smoke'
assert_json_contains "$KNOWLEDGE_REFRESH_JSON" '\"operation\":\"refresh\"' 'knowledge refresh payload'
assert_json_contains "$KNOWLEDGE_REFRESH_JSON" '\"status\":\"accepted\"' 'knowledge refresh status'
write_artifact_file 'knowledge-refresh.json' "$KNOWLEDGE_REFRESH_JSON"

KNOWLEDGE_HEALTH_READY_JSON=$(wait_for_knowledge_refresh_ready)
assert_knowledge_health_ready "$KNOWLEDGE_HEALTH_READY_JSON" 'knowledge health readiness smoke'
write_artifact_file 'knowledge-health-ready.json' "$KNOWLEDGE_HEALTH_READY_JSON"

KNOWLEDGE_RETRIEVE_JSON=$(run_root dasall knowledge retrieve "$PROVIDER_QUERY" --json --timeout-ms "$TIMEOUT_MS")
assert_json_contains "$KNOWLEDGE_RETRIEVE_JSON" '"disposition":"completed"' 'knowledge retrieve smoke'
assert_json_contains "$KNOWLEDGE_RETRIEVE_JSON" '\"operation\":\"retrieve\"' 'knowledge retrieve payload'
assert_json_contains "$KNOWLEDGE_RETRIEVE_JSON" '\"ok\":true' 'knowledge retrieve ok'
assert_json_matches "$KNOWLEDGE_RETRIEVE_JSON" '\\"slice_count\\":[1-9][0-9]*' 'knowledge retrieve slice count'
assert_json_matches "$KNOWLEDGE_RETRIEVE_JSON" 'DeepSeek Chat|deepseek-chat|llm/providers/deepseek/' 'knowledge retrieve installed provider evidence'
write_artifact_file 'knowledge-retrieve-provider.json' "$KNOWLEDGE_RETRIEVE_JSON"

KNOWLEDGE_HEALTH_FINAL_JSON=$(run_root dasall knowledge health --json --timeout-ms "$TIMEOUT_MS")
assert_knowledge_health_ready "$KNOWLEDGE_HEALTH_FINAL_JSON" 'knowledge health smoke'
write_artifact_file 'knowledge-health-final.json' "$KNOWLEDGE_HEALTH_FINAL_JSON"

run_root test -f /usr/share/dasall/docs/architecture/DASALL_Engineering_Blueprint.md
run_root test -f /usr/share/dasall/docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md
run_root test -f /usr/share/dasall/docs/ssot/BusinessChainIntegrationMatrix.md
cat > "$ARTIFACT_DIR/installed-normative-assets.json" <<'EOF'
{
  "architecture_blueprint": "/usr/share/dasall/docs/architecture/DASALL_Engineering_Blueprint.md",
  "adr_006": "/usr/share/dasall/docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md",
  "ssot_business_chain_matrix": "/usr/share/dasall/docs/ssot/BusinessChainIntegrationMatrix.md"
}
EOF
write_proof_summary

log 'knowledge local installed proof passed'