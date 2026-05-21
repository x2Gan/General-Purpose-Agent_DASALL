#!/bin/sh
set -eu

ARTIFACT_DIR=${DASALL_CAPABILITY_SERVICES_PROOF_ARTIFACT_DIR:-/tmp/dasall-capability-services-proof}
TOOLS_PROOF_JSON_PATH=${DASALL_CAPABILITY_SERVICES_TOOLS_PROOF_JSON:-}
PACKAGE_SMOKE_ARTIFACT_DIR=${DASALL_PACKAGE_SMOKE_ARTIFACT_DIR:-}

log() {
  printf '[services-proof] %s\n' "$*"
}

fail() {
  printf '[services-proof] %s\n' "$*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Usage: bash scripts/packaging/services_local_installed_proof.sh [--artifact-dir <path>] [--tool-proof-json <path>]
EOF
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || fail "missing required command: $1"
}

ensure_artifact_dir() {
  mkdir -p "$ARTIFACT_DIR"
}

resolve_tool_proof_json_path() {
  if [ -n "$TOOLS_PROOF_JSON_PATH" ]; then
    printf '%s\n' "$TOOLS_PROOF_JSON_PATH"
    return 0
  fi

  if [ -n "$PACKAGE_SMOKE_ARTIFACT_DIR" ]; then
    printf '%s\n' "$PACKAGE_SMOKE_ARTIFACT_DIR/tools-installed-proof.json"
    return 0
  fi

  fail 'missing tools installed proof source; pass --tool-proof-json or export DASALL_PACKAGE_SMOKE_ARTIFACT_DIR'
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --artifact-dir)
      [ "$#" -ge 2 ] || fail 'missing value for --artifact-dir'
      ARTIFACT_DIR=$2
      shift 2
      ;;
    --tool-proof-json)
      [ "$#" -ge 2 ] || fail 'missing value for --tool-proof-json'
      TOOLS_PROOF_JSON_PATH=$2
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

require_command cp
require_command python3
ensure_artifact_dir

SOURCE_TOOL_PROOF=$(resolve_tool_proof_json_path)
[ -f "$SOURCE_TOOL_PROOF" ] || fail "missing tools installed proof source: $SOURCE_TOOL_PROOF"

cp "$SOURCE_TOOL_PROOF" "$ARTIFACT_DIR/source-tools-installed-proof.json"

python3 - "$SOURCE_TOOL_PROOF" "$ARTIFACT_DIR/services-installed-proof.json" <<'PY'
import json
import pathlib
import sys

source_path = pathlib.Path(sys.argv[1])
output_path = pathlib.Path(sys.argv[2])
payload = json.loads(source_path.read_text(encoding='utf-8'))


def require_true(name: str) -> None:
    if payload.get(name) is not True:
        raise SystemExit(f'{name} must be true')


def require_equal(name: str, expected: str) -> str:
    value = payload.get(name)
    if value != expected:
        raise SystemExit(f'{name} must equal {expected!r}, got {value!r}')
    return value


def require_contains(text: str, expected: str, label: str) -> None:
    if expected not in text:
        raise SystemExit(f'{label} missing {expected!r}')


for field_name in [
    'ok',
    'agent_dataset_visible',
    'agent_terminal_visible',
    'tool_invocation_succeeded',
    'terminal_confirmation_denied',
    'terminal_invocation_succeeded',
    'projection_present',
    'terminal_projection_present',
    'route_citation_present',
    'tool_call_citation_present',
    'production_bridge_evidence_present',
    'production_observability_evidence_present',
]:
    require_true(field_name)

data_route_kind = require_equal('route_kind', 'builtin')
terminal_route_kind = require_equal('terminal_route_kind', 'builtin')

visible_tools = payload.get('visible_tools')
if not isinstance(visible_tools, list):
    raise SystemExit('visible_tools must be a list')
for tool_name in ['agent.dataset', 'agent.terminal']:
    if tool_name not in visible_tools:
        raise SystemExit(f'visible_tools missing {tool_name!r}')

external_evidence = payload.get('external_evidence')
if not isinstance(external_evidence, list):
    raise SystemExit('external_evidence must be a list')
for evidence_ref in [
    'runtime:daemon.local-control-plane:tool-services-production-bridge',
    'runtime:daemon.local-control-plane:production-observability-health',
]:
    if evidence_ref not in external_evidence:
        raise SystemExit(f'external_evidence missing {evidence_ref!r}')

data_payload = payload.get('payload', '')
terminal_payload = payload.get('terminal_payload', '')
if not isinstance(data_payload, str) or not isinstance(terminal_payload, str):
    raise SystemExit('payload and terminal_payload must be strings')

require_contains(data_payload, '"capability_id":"agent.dataset"', 'payload')
require_contains(data_payload, '"projection":"default"', 'payload')
require_contains(terminal_payload, '"operation":"agent.terminal"', 'terminal_payload')

summary = {
    'source_tool_proof_path': str(source_path),
    'effective_profile_id': payload.get('effective_profile_id', ''),
    'visible_tools': visible_tools,
    'external_evidence': external_evidence,
    'data_route_kind': data_route_kind,
    'terminal_route_kind': terminal_route_kind,
    'service_payload_evidence_present': True,
    'service_confirmation_gate_present': True,
    'service_projection_evidence_present': True,
    'service_route_citation_present': True,
    'service_tool_call_citation_present': True,
    'service_bridge_evidence_present': True,
    'service_observability_evidence_present': True,
    'tool_to_services_adapter_backend_path_present': True,
    'terminal_failure_reason_code': payload.get('terminal_failure_reason_code', ''),
    'authoritative_owner': 'pkg_smoke_install.sh -> tools-installed-proof.json',
    'data_payload_preview': data_payload[:160],
    'terminal_payload_preview': terminal_payload[:160],
}

output_path.write_text(json.dumps(summary, indent=2) + '\n', encoding='ascii')
PY

printf '%s\n' 'capability services local installed proof passed' > "$ARTIFACT_DIR/status.txt"
log "wrote capability services installed proof artifacts to ${ARTIFACT_DIR}"