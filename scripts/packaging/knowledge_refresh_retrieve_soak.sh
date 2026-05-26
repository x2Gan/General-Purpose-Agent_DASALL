#!/bin/sh
set -eu

ARTIFACT_DIR=${DASALL_KNOWLEDGE_SOAK_ARTIFACT_DIR:-/tmp/dasall-knowledge-soak}
ITERATIONS=${DASALL_KNOWLEDGE_SOAK_ITERATIONS:-10}
TIMEOUT_MS=${DASALL_KNOWLEDGE_SOAK_TIMEOUT_MS:-30000}
DAEMON_READY_ATTEMPTS=${DASALL_KNOWLEDGE_SOAK_DAEMON_READY_ATTEMPTS:-90}
KNOWLEDGE_READY_ATTEMPTS=${DASALL_KNOWLEDGE_SOAK_KNOWLEDGE_READY_ATTEMPTS:-60}
PROVIDER_QUERY=${DASALL_KNOWLEDGE_SOAK_PROVIDER_QUERY:-DeepSeek Chat}
HYBRID_CANARY=0
HYBRID_CANARY_QUERY=${DASALL_KNOWLEDGE_HYBRID_CANARY_QUERY:-ContextOrchestrator PromptComposer}
HYBRID_CANARY_ALLOWED_CORPUS=${DASALL_KNOWLEDGE_HYBRID_CANARY_ALLOWED_CORPUS:-adr_normative}
HYBRID_CANARY_QUERY_KIND=${DASALL_KNOWLEDGE_HYBRID_CANARY_QUERY_KIND:-policy-evidence}
SYSTEMD_DROPIN_DIR=/run/systemd/system/dasall-daemon.service.d
SYSTEMD_DROPIN_PATH=${SYSTEMD_DROPIN_DIR}/knowledge-hybrid-canary.conf

log() {
  printf '[knowledge-soak] %s\n' "$*"
}

fail() {
  printf '[knowledge-soak] %s\n' "$*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Usage: bash scripts/packaging/knowledge_refresh_retrieve_soak.sh [--artifact-dir <path>] [--iterations <count>] [--timeout-ms <value>] [--hybrid-canary]
EOF
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || fail "missing required command: $1"
}

require_root_access() {
  if [ "$(id -u)" -eq 0 ]; then
    return 0
  fi

  command -v sudo >/dev/null 2>&1 || fail 'root or sudo is required'
  sudo -n true >/dev/null 2>&1 || fail 'passwordless sudo is required for knowledge soak; rerun as root'
}

run_root() {
  require_root_access
  if [ "$(id -u)" -eq 0 ]; then
    "$@"
  else
    sudo "$@"
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
  assert_json_payload=$1
  assert_expected=$2
  assert_label=$3

  printf '%s\n' "$assert_json_payload" | grep -Fq "$assert_expected" || \
    fail "${assert_label}: expected JSON fragment not found: ${assert_expected}"
}

assert_json_matches() {
  assert_json_payload=$1
  assert_expected_regex=$2
  assert_label=$3

  printf '%s\n' "$assert_json_payload" | grep -Eq "$assert_expected_regex" || \
    fail "${assert_label}: expected JSON pattern not found: ${assert_expected_regex}"
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
  health_payload=$1
  health_label=$2

  knowledge_health_is_ready "$health_payload" || fail "${health_label}: health payload did not reach a ready state: ${health_payload}"
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
    if readiness_json=$(run_root dasall-cli readiness --json --timeout-ms "$TIMEOUT_MS" 2>/dev/null); then
      if readiness_is_ready "$readiness_json"; then
        printf '%s\n' "$readiness_json"
        return 0
      fi
    fi
    attempts=$((attempts + 1))
    sleep 1
  done

  fail 'timed out waiting for dasall-daemon readiness'
}

wait_for_knowledge_refresh_ready() {
  attempts=0
  while [ "$attempts" -lt "$KNOWLEDGE_READY_ATTEMPTS" ]; do
    if knowledge_health_json=$(run_root dasall-cli knowledge health --json --timeout-ms "$TIMEOUT_MS" 2>/dev/null); then
      if knowledge_health_is_ready "$knowledge_health_json"; then
        printf '%s\n' "$knowledge_health_json"
        return 0
      fi
      if json_contains_fragment "$knowledge_health_json" '"last_refresh_status":"failed"'; then
        fail "knowledge refresh terminal failure: ${knowledge_health_json}"
      fi
    fi
    attempts=$((attempts + 1))
    sleep 1
  done

  fail 'timed out waiting for async knowledge refresh completion'
}

cleanup() {
  run_root systemctl disable --now dasall-daemon.service >/dev/null 2>&1 || true
  if [ "$HYBRID_CANARY" -eq 1 ]; then
    run_root sh -eu -c "
      rm -f '${SYSTEMD_DROPIN_PATH}'
      rmdir --ignore-fail-on-non-empty '${SYSTEMD_DROPIN_DIR}' >/dev/null 2>&1 || true
      systemctl daemon-reload
    " >/dev/null 2>&1 || true
  fi
}

enable_hybrid_canary_runtime() {
  [ "$HYBRID_CANARY" -eq 1 ] || return 0

  log 'enabling temporary detached vector local fallback for hybrid canary'
  run_root sh -eu -c "
    install -d -m 0755 '${SYSTEMD_DROPIN_DIR}'
    cat > '${SYSTEMD_DROPIN_PATH}' <<'EOF'
[Service]
Environment=DASALL_DETACHED_VECTOR_LOCAL_FALLBACK=1
EOF
    systemctl daemon-reload
  "
}

start_daemon() {
  run_root systemctl enable --now dasall-daemon.service >/dev/null 2>&1 || fail 'failed to enable/start dasall-daemon.service'
}

run_iteration() {
  iteration=$1
  label=$(printf '%02d' "$iteration")

  refresh_json=$(run_root dasall-cli knowledge refresh --json --timeout-ms "$TIMEOUT_MS")
  assert_json_contains "$refresh_json" '"disposition":"completed"' "iteration ${label} refresh disposition"
  assert_json_contains "$refresh_json" '\"operation\":\"refresh\"' "iteration ${label} refresh payload"
  assert_json_contains "$refresh_json" '\"status\":\"accepted\"' "iteration ${label} refresh status"
  write_artifact_file "iteration-${label}-refresh.json" "$refresh_json"

  health_ready_json=$(wait_for_knowledge_refresh_ready)
  assert_knowledge_health_ready "$health_ready_json" "iteration ${label} ready health"
  write_artifact_file "iteration-${label}-health-ready.json" "$health_ready_json"

  provider_json=$(run_root dasall-cli knowledge retrieve "$PROVIDER_QUERY" --json --timeout-ms "$TIMEOUT_MS")
  assert_json_contains "$provider_json" '"disposition":"completed"' "iteration ${label} provider retrieve"
  assert_json_contains "$provider_json" '\"ok\":true' "iteration ${label} provider retrieve ok"
  assert_json_matches "$provider_json" '\\"slice_count\\":[1-9][0-9]*' "iteration ${label} provider slice count"
  assert_json_matches "$provider_json" 'DeepSeek Chat|deepseek-chat|llm/providers/deepseek/' "iteration ${label} provider evidence"
  write_artifact_file "iteration-${label}-retrieve-provider.json" "$provider_json"

  if [ "$HYBRID_CANARY" -eq 1 ]; then
    hybrid_json=$(run_root dasall-cli knowledge retrieve \
      "$HYBRID_CANARY_QUERY" \
      --preferred-mode hybrid \
      --query-kind "$HYBRID_CANARY_QUERY_KIND" \
      --allowed-corpus "$HYBRID_CANARY_ALLOWED_CORPUS" \
      --json \
      --timeout-ms "$TIMEOUT_MS")
    write_artifact_file "iteration-${label}-retrieve-hybrid-canary.json" "$hybrid_json"
    assert_json_contains "$hybrid_json" '"disposition":"completed"' "iteration ${label} hybrid canary retrieve"
    assert_json_contains "$hybrid_json" '\"operation\":\"retrieve\"' "iteration ${label} hybrid canary payload"
    assert_json_contains "$hybrid_json" '\"ok\":true' "iteration ${label} hybrid canary ok"
    assert_json_matches "$hybrid_json" '\\"slice_count\\":[1-9][0-9]*' "iteration ${label} hybrid canary slice count"
    assert_json_matches "$hybrid_json" 'selected_corpora|corpus_summary' "iteration ${label} hybrid corpus summary"
    assert_json_matches "$hybrid_json" '\\"vector_backend_ready\\":(true|false)' "iteration ${label} hybrid vector backend marker"
    assert_json_matches "$hybrid_json" '\\"sparse_hit_count\\":[0-9]+' "iteration ${label} hybrid sparse hit count"
    assert_json_matches "$hybrid_json" '\\"dense_hit_count\\":[0-9]+' "iteration ${label} hybrid dense hit count"
  fi

  health_final_json=$(run_root dasall-cli knowledge health --json --timeout-ms "$TIMEOUT_MS")
  assert_knowledge_health_ready "$health_final_json" "iteration ${label} final health"
  write_artifact_file "iteration-${label}-health-final.json" "$health_final_json"
}

write_summary() {
  python3 - "$ARTIFACT_DIR" "$ITERATIONS" "$PROVIDER_QUERY" "$HYBRID_CANARY" \
    "$HYBRID_CANARY_QUERY" "$HYBRID_CANARY_ALLOWED_CORPUS" <<'PY'
import json
import pathlib
import re
import sys

artifact_dir = pathlib.Path(sys.argv[1])
iterations_requested = int(sys.argv[2])
provider_query = sys.argv[3]
hybrid_canary_enabled = sys.argv[4] == '1'
hybrid_canary_query = sys.argv[5]
hybrid_canary_allowed_corpus = sys.argv[6]

def read_text(path: pathlib.Path) -> str:
    return path.read_text(encoding='utf-8')

def extract(pattern: str, text: str, label: str) -> str:
    match = re.search(pattern, text)
    if not match:
        raise SystemExit(f'missing {label}')
    return match.group(1)

def read_optional_summary(path: pathlib.Path) -> dict[str, str]:
    if not path.exists():
        return {}
    text = read_text(path)
    summary: dict[str, str] = {}
    for pattern, key in [
    (r'\\"state\\":\\"([^\\]+)\\"', 'state'),
    (r'\\"runtime_readiness\\":\\"([^\\]+)\\"', 'runtime_readiness'),
    ]:
        match = re.search(pattern, text)
        if match:
            summary[key] = match.group(1)
    return summary

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

iteration_summaries = []
for iteration in range(1, iterations_requested + 1):
    label = f'{iteration:02d}'
    refresh_text = read_text(artifact_dir / f'iteration-{label}-refresh.json')
    ready_text = read_text(artifact_dir / f'iteration-{label}-health-ready.json')
    provider_text = read_text(artifact_dir / f'iteration-{label}-retrieve-provider.json')
    final_text = read_text(artifact_dir / f'iteration-{label}-health-final.json')
    iteration_summary = {
      'iteration': iteration,
      'refresh_disposition': extract(r'"disposition":"([^"]+)"', refresh_text, f'iteration {label} refresh disposition'),
      'refresh_status': extract(r'\\"status\\":\\"([^\\]+)\\"', refresh_text, f'iteration {label} refresh status'),
        'ready_signal': 'async_terminal' if optional_extract(r'\\"last_refresh_status\\":\\"([^\\]+)\\"', ready_text) else 'snapshot_freshness',
        'ready_state': extract(r'\\"state\\":\\"([^\\]+)\\"', ready_text, f'iteration {label} ready state'),
        'ready_freshness_state': extract(r'\\"freshness_state\\":\\"([^\\]+)\\"', ready_text, f'iteration {label} ready freshness_state'),
        'ready_last_refresh_status': optional_extract(r'\\"last_refresh_status\\":\\"([^\\]+)\\"', ready_text),
      'ready_active_snapshot_id': extract(r'\\"active_snapshot_id\\":\\"([^\\]+)\\"', ready_text, f'iteration {label} ready snapshot'),
      'provider_slice_count': int(extract(r'\\"slice_count\\":([0-9]+)', provider_text, f'iteration {label} provider slice count')),
      'provider_has_expected_evidence': bool(re.search(r'DeepSeek Chat|deepseek-chat|llm/providers/deepseek/', provider_text)),
        'final_state': extract(r'\\"state\\":\\"([^\\]+)\\"', final_text, f'iteration {label} final state'),
        'final_freshness_state': extract(r'\\"freshness_state\\":\\"([^\\]+)\\"', final_text, f'iteration {label} final freshness_state'),
      'final_active_snapshot_id': extract(r'\\"active_snapshot_id\\":\\"([^\\]+)\\"', final_text, f'iteration {label} final snapshot'),
        'final_refresh_in_flight': (
          optional_extract(r'\\"refresh_in_flight\\":(true|false)', final_text) == 'true'
          if optional_extract(r'\\"refresh_in_flight\\":(true|false)', final_text) is not None
          else None
        ),
    }
    if hybrid_canary_enabled:
      hybrid_text = read_text(artifact_dir / f'iteration-{label}-retrieve-hybrid-canary.json')
      iteration_summary.update(
        {
          'hybrid_mode': extract(r'\\"mode\\":\\"([^\\]+)\\"', hybrid_text, f'iteration {label} hybrid mode'),
          'hybrid_degraded': extract_bool(r'\\"degraded\\":(true|false)', hybrid_text, f'iteration {label} hybrid degraded'),
          'hybrid_reason_codes': extract_json_array(r'\\"reason_codes\\":(\[.*?\])', hybrid_text, f'iteration {label} hybrid reason_codes'),
          'hybrid_warning_summary': optional_json_array(r'\\"warning_summary\\":(\[.*?\])', hybrid_text) or [],
          'hybrid_selected_corpora': (
            optional_json_array(r'\\"selected_corpora\\":(\[.*?\])', hybrid_text)
            or extract_json_array(r'\\"corpus_summary\\":(\[.*?\])', hybrid_text, f'iteration {label} hybrid corpus summary')
          ),
          'hybrid_vector_backend_ready': extract_bool(
            r'\\"vector_backend_ready\\":(true|false)',
            hybrid_text,
            f'iteration {label} hybrid vector backend ready',
          ),
          'hybrid_sparse_hit_count': int(
            extract(r'\\"sparse_hit_count\\":([0-9]+)', hybrid_text, f'iteration {label} hybrid sparse_hit_count')
          ),
          'hybrid_dense_hit_count': int(
            extract(r'\\"dense_hit_count\\":([0-9]+)', hybrid_text, f'iteration {label} hybrid dense_hit_count')
          ),
        }
      )
      iteration_summary['hybrid_has_dense_artifact'] = iteration_summary['hybrid_dense_hit_count'] > 0
    iteration_summaries.append(iteration_summary)

summary = {
    'iterations_requested': iterations_requested,
    'iterations_completed': len(iteration_summaries),
    'provider_query': provider_query,
    'hybrid_canary_enabled': hybrid_canary_enabled,
    'min_provider_slice_count': min(item['provider_slice_count'] for item in iteration_summaries),
    'all_refresh_status_accepted': all(item['refresh_status'] == 'accepted' for item in iteration_summaries),
    'ready_signals': sorted({item['ready_signal'] for item in iteration_summaries}),
    'ready_states': sorted({item['ready_state'] for item in iteration_summaries}),
    'all_ready_health_freshness_fresh': all(item['ready_freshness_state'] == 'fresh' for item in iteration_summaries),
    'all_ready_health_have_active_snapshot': all(bool(item['ready_active_snapshot_id']) for item in iteration_summaries),
    'all_provider_iterations_have_evidence': all(item['provider_has_expected_evidence'] for item in iteration_summaries),
    'final_states': sorted({item['final_state'] for item in iteration_summaries}),
    'all_final_health_have_active_snapshot': all(bool(item['final_active_snapshot_id']) for item in iteration_summaries),
    'all_final_health_freshness_fresh': all(item['final_freshness_state'] == 'fresh' for item in iteration_summaries),
    'all_final_health_refresh_in_flight_false_or_absent': all(item['final_refresh_in_flight'] in (False, None) for item in iteration_summaries),
    'unique_active_snapshot_ids': sorted({item['final_active_snapshot_id'] for item in iteration_summaries}),
    'pre_ready': read_optional_summary(artifact_dir / 'pre-ready.json'),
    'post_ready': read_optional_summary(artifact_dir / 'post-ready.json'),
}

if hybrid_canary_enabled:
    summary.update(
      {
        'hybrid_canary_query': hybrid_canary_query,
        'hybrid_canary_allowed_corpus': hybrid_canary_allowed_corpus,
        'hybrid_mode_values': sorted({item['hybrid_mode'] for item in iteration_summaries}),
        'all_hybrid_iterations_admitted': all('runtime_canary_admitted' in item['hybrid_reason_codes'] for item in iteration_summaries),
        'all_hybrid_iterations_have_selected_corpora': all(bool(item['hybrid_selected_corpora']) for item in iteration_summaries),
        'all_hybrid_iterations_vector_backend_ready': all(item['hybrid_vector_backend_ready'] for item in iteration_summaries),
        'all_hybrid_iterations_record_dense_artifact_presence': all(
            'hybrid_has_dense_artifact' in item for item in iteration_summaries
        ),
        'any_hybrid_iteration_has_dense_artifact': any(item['hybrid_has_dense_artifact'] for item in iteration_summaries),
        'min_hybrid_dense_hit_count': min(item['hybrid_dense_hit_count'] for item in iteration_summaries),
        'hybrid_reason_code_union': sorted({reason for item in iteration_summaries for reason in item['hybrid_reason_codes']}),
      }
    )

if summary['iterations_completed'] != iterations_requested:
    raise SystemExit('knowledge soak did not complete all requested iterations')
if not summary['all_refresh_status_accepted']:
    raise SystemExit('knowledge soak observed a refresh status different from accepted')
if not summary['all_ready_health_freshness_fresh']:
  raise SystemExit('knowledge soak observed a non-fresh health readiness state')
if not summary['all_ready_health_have_active_snapshot']:
  raise SystemExit('knowledge soak observed readiness without an active snapshot')
if not summary['all_provider_iterations_have_evidence']:
    raise SystemExit('knowledge soak observed a provider retrieve without installed evidence')
if not summary['all_final_health_have_active_snapshot']:
  raise SystemExit('knowledge soak observed final health without an active snapshot')
if not summary['all_final_health_freshness_fresh']:
  raise SystemExit('knowledge soak observed a non-fresh final health state')
if not summary['all_final_health_refresh_in_flight_false_or_absent']:
  raise SystemExit('knowledge soak observed refresh_in_flight after final health probe')
if hybrid_canary_enabled and not summary['all_hybrid_iterations_have_selected_corpora']:
    raise SystemExit('knowledge soak observed a hybrid canary iteration without selected corpora')

(artifact_dir / 'knowledge-soak-summary.json').write_text(
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

ensure_positive_integer "$ITERATIONS" 'iterations'
ensure_positive_integer "$TIMEOUT_MS" 'timeout-ms'
ensure_positive_integer "$DAEMON_READY_ATTEMPTS" 'daemon-ready-attempts'
ensure_positive_integer "$KNOWLEDGE_READY_ATTEMPTS" 'knowledge-ready-attempts'
require_command dasall-cli
require_command systemctl
ensure_artifact_dir

trap cleanup EXIT INT TERM

log "writing knowledge soak artifacts to ${ARTIFACT_DIR}"
capture_journal 'pre.log'
enable_hybrid_canary_runtime
start_daemon
PRE_READY_JSON=$(wait_for_daemon_ready)
write_artifact_file 'pre-ready.json' "$PRE_READY_JSON"

iteration=1
while [ "$iteration" -le "$ITERATIONS" ]; do
  log "running knowledge soak iteration ${iteration}/${ITERATIONS}"
  run_iteration "$iteration"
  iteration=$((iteration + 1))
done

POST_READY_JSON=$(wait_for_daemon_ready)
write_artifact_file 'post-ready.json' "$POST_READY_JSON"
capture_journal 'post.log'
write_summary
write_artifact_file 'status.txt' 'knowledge soak passed'
log 'knowledge soak passed'