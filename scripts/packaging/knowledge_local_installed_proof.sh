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
HYBRID_CANARY=0
HYBRID_CANARY_QUERY=${DASALL_KNOWLEDGE_HYBRID_CANARY_QUERY:-ContextOrchestrator PromptComposer}
HYBRID_CANARY_ALLOWED_CORPUS=${DASALL_KNOWLEDGE_HYBRID_CANARY_ALLOWED_CORPUS:-adr_normative}
HYBRID_CANARY_QUERY_KIND=${DASALL_KNOWLEDGE_HYBRID_CANARY_QUERY_KIND:-policy-evidence}
SYSTEMD_DROPIN_DIR=/run/systemd/system/dasall-daemon.service.d
SYSTEMD_DROPIN_PATH=${SYSTEMD_DROPIN_DIR}/knowledge-hybrid-canary.conf

log() {
  printf '[knowledge-proof] %s\n' "$*"
}

fail() {
  printf '[knowledge-proof] %s\n' "$*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Usage: bash scripts/packaging/knowledge_local_installed_proof.sh [--artifact-dir <path>] [--timeout-ms <value>] [--hybrid-canary]
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

wait_for_knowledge_refresh_ready() {
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
  if [ "$HYBRID_CANARY" -eq 1 ]; then
    run_root_sh "
      rm -f '${SYSTEMD_DROPIN_PATH}'
      rmdir --ignore-fail-on-non-empty '${SYSTEMD_DROPIN_DIR}' >/dev/null 2>&1 || true
      systemctl daemon-reload
    " >/dev/null 2>&1 || true
  fi
}

enable_hybrid_canary_runtime() {
  [ "$HYBRID_CANARY" -eq 1 ] || return 0

  log 'enabling temporary detached vector local fallback for hybrid canary'
  run_root_sh "
    install -d -m 0755 '${SYSTEMD_DROPIN_DIR}'
    cat > '${SYSTEMD_DROPIN_PATH}' <<'EOF'
[Service]
Environment=DASALL_DETACHED_VECTOR_LOCAL_FALLBACK=1
EOF
    systemctl daemon-reload
  "
}

write_proof_summary() {
  python3 - "$ARTIFACT_DIR" "$PROVIDER_QUERY" "$HYBRID_CANARY" \
    "$HYBRID_CANARY_QUERY" "$HYBRID_CANARY_ALLOWED_CORPUS" <<'PY'
import json
import pathlib
import re
import sys

artifact_dir = pathlib.Path(sys.argv[1])
provider_query = sys.argv[2]
hybrid_canary_enabled = sys.argv[3] == '1'
hybrid_canary_query = sys.argv[4]
hybrid_canary_allowed_corpus = sys.argv[5]

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

def extract_bool(pattern: str, text: str, label: str) -> bool:
  return extract(pattern, text, label) == 'true'

def extract_json_array(pattern: str, text: str, label: str):
  raw = extract(pattern, text, label)
  return json.loads(raw.replace('\\"', '"'))

def optional_json_array(pattern: str, text: str):
  raw = optional_extract(pattern, text)
  if raw is None:
    return None
  return json.loads(raw.replace('\\"', '"'))

ready_text = read_text('ready.json')
refresh_text = read_text('knowledge-refresh.json')
health_ready_text = read_text('knowledge-health-ready.json')
provider_text = read_text('knowledge-retrieve-provider.json')
health_final_text = read_text('knowledge-health-final.json')
hybrid_text = read_text('knowledge-retrieve-hybrid-canary.json') if hybrid_canary_enabled else None

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
    'hybrid_canary_enabled': hybrid_canary_enabled,
    'health_final_state': extract(r'\\"state\\":\\"([^\\]+)\\"', health_final_text, 'health final state'),
    'health_final_freshness_state': extract(r'\\"freshness_state\\":\\"([^\\]+)\\"', health_final_text, 'health final freshness_state'),
    'health_final_active_snapshot_id': extract(r'\\"active_snapshot_id\\":\\"([^\\]+)\\"', health_final_text, 'health final active snapshot'),
    'health_final_refresh_in_flight': (
        optional_extract(r'\\"refresh_in_flight\\":(true|false)', health_final_text) == 'true'
        if optional_extract(r'\\"refresh_in_flight\\":(true|false)', health_final_text) is not None
        else None
    ),
}

if hybrid_canary_enabled:
    data.update(
        {
            'hybrid_canary_query': hybrid_canary_query,
            'hybrid_canary_allowed_corpus': hybrid_canary_allowed_corpus,
            'hybrid_canary_mode': extract(r'\\"mode\\":\\"([^\\]+)\\"', hybrid_text, 'hybrid canary mode'),
            'hybrid_canary_degraded': extract_bool(r'\\"degraded\\":(true|false)', hybrid_text, 'hybrid canary degraded'),
            'hybrid_canary_reason_codes': extract_json_array(r'\\"reason_codes\\":(\[.*?\])', hybrid_text, 'hybrid canary reason_codes'),
            'hybrid_canary_warning_summary': optional_json_array(r'\\"warning_summary\\":(\[.*?\])', hybrid_text) or [],
            'hybrid_canary_selected_corpora': (
              optional_json_array(r'\\"selected_corpora\\":(\[.*?\])', hybrid_text)
              or extract_json_array(r'\\"corpus_summary\\":(\[.*?\])', hybrid_text, 'hybrid canary corpus summary')
            ),
            'hybrid_canary_vector_backend_ready': extract_bool(
                r'\\"vector_backend_ready\\":(true|false)',
                hybrid_text,
                'hybrid canary vector backend ready',
            ),
            'hybrid_canary_sparse_hit_count': int(
                extract(r'\\"sparse_hit_count\\":([0-9]+)', hybrid_text, 'hybrid canary sparse_hit_count')
            ),
            'hybrid_canary_dense_hit_count': int(
                extract(r'\\"dense_hit_count\\":([0-9]+)', hybrid_text, 'hybrid canary dense_hit_count')
            ),
        }
    )
    data['hybrid_canary_has_dense_artifact'] = data['hybrid_canary_dense_hit_count'] > 0

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
    --hybrid-canary)
      HYBRID_CANARY=1
      shift 1
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
enable_hybrid_canary_runtime

run_root systemctl enable --now dasall-daemon.service
READY_JSON=$(wait_for_daemon_ready)
write_artifact_file 'ready.json' "$READY_JSON"

KNOWLEDGE_REFRESH_JSON=$(run_root dasall-cli knowledge refresh --json --timeout-ms "$TIMEOUT_MS")
assert_json_contains "$KNOWLEDGE_REFRESH_JSON" '"disposition":"completed"' 'knowledge refresh smoke'
assert_json_contains "$KNOWLEDGE_REFRESH_JSON" '\"operation\":\"refresh\"' 'knowledge refresh payload'
assert_json_contains "$KNOWLEDGE_REFRESH_JSON" '\"status\":\"accepted\"' 'knowledge refresh status'
write_artifact_file 'knowledge-refresh.json' "$KNOWLEDGE_REFRESH_JSON"

KNOWLEDGE_HEALTH_READY_JSON=$(wait_for_knowledge_refresh_ready)
assert_knowledge_health_ready "$KNOWLEDGE_HEALTH_READY_JSON" 'knowledge health readiness smoke'
write_artifact_file 'knowledge-health-ready.json' "$KNOWLEDGE_HEALTH_READY_JSON"

KNOWLEDGE_RETRIEVE_JSON=$(run_root dasall-cli knowledge retrieve "$PROVIDER_QUERY" --json --timeout-ms "$TIMEOUT_MS")
assert_json_contains "$KNOWLEDGE_RETRIEVE_JSON" '"disposition":"completed"' 'knowledge retrieve smoke'
assert_json_contains "$KNOWLEDGE_RETRIEVE_JSON" '\"operation\":\"retrieve\"' 'knowledge retrieve payload'
assert_json_contains "$KNOWLEDGE_RETRIEVE_JSON" '\"ok\":true' 'knowledge retrieve ok'
assert_json_matches "$KNOWLEDGE_RETRIEVE_JSON" '\\"slice_count\\":[1-9][0-9]*' 'knowledge retrieve slice count'
assert_json_matches "$KNOWLEDGE_RETRIEVE_JSON" 'DeepSeek Chat|deepseek-chat|llm/providers/deepseek/' 'knowledge retrieve installed provider evidence'
write_artifact_file 'knowledge-retrieve-provider.json' "$KNOWLEDGE_RETRIEVE_JSON"

if [ "$HYBRID_CANARY" -eq 1 ]; then
  KNOWLEDGE_RETRIEVE_HYBRID_CANARY_JSON=$(run_root dasall-cli knowledge retrieve \
    "$HYBRID_CANARY_QUERY" \
    --preferred-mode hybrid \
    --query-kind "$HYBRID_CANARY_QUERY_KIND" \
    --allowed-corpus "$HYBRID_CANARY_ALLOWED_CORPUS" \
    --json \
    --timeout-ms "$TIMEOUT_MS")
  write_artifact_file 'knowledge-retrieve-hybrid-canary.json' "$KNOWLEDGE_RETRIEVE_HYBRID_CANARY_JSON"
  assert_json_contains "$KNOWLEDGE_RETRIEVE_HYBRID_CANARY_JSON" '"disposition":"completed"' 'knowledge hybrid canary retrieve smoke'
  assert_json_contains "$KNOWLEDGE_RETRIEVE_HYBRID_CANARY_JSON" '\"operation\":\"retrieve\"' 'knowledge hybrid canary payload'
  assert_json_contains "$KNOWLEDGE_RETRIEVE_HYBRID_CANARY_JSON" '\"ok\":true' 'knowledge hybrid canary ok'
  assert_json_matches "$KNOWLEDGE_RETRIEVE_HYBRID_CANARY_JSON" '\\"slice_count\\":[1-9][0-9]*' 'knowledge hybrid canary slice count'
  assert_json_matches "$KNOWLEDGE_RETRIEVE_HYBRID_CANARY_JSON" 'selected_corpora|corpus_summary' 'knowledge hybrid canary corpus summary'
  assert_json_matches "$KNOWLEDGE_RETRIEVE_HYBRID_CANARY_JSON" '\\"vector_backend_ready\\":(true|false)' 'knowledge hybrid canary vector backend marker'
  assert_json_matches "$KNOWLEDGE_RETRIEVE_HYBRID_CANARY_JSON" '\\"sparse_hit_count\\":[0-9]+' 'knowledge hybrid canary sparse hit count'
  assert_json_matches "$KNOWLEDGE_RETRIEVE_HYBRID_CANARY_JSON" '\\"dense_hit_count\\":[0-9]+' 'knowledge hybrid canary dense hit count'
fi

KNOWLEDGE_HEALTH_FINAL_JSON=$(run_root dasall-cli knowledge health --json --timeout-ms "$TIMEOUT_MS")
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