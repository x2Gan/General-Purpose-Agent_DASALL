#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "${SCRIPT_DIR}/../.." && pwd)
DEFAULT_EVIDENCE_ROOT="${HOME}/.cache/dasall/memory/installed-evidence"

EVIDENCE_ROOT=${DASALL_MEMORY_INSTALLED_EVIDENCE_ROOT:-${DEFAULT_EVIDENCE_ROOT}}
ARTIFACT_DIR=
QEMU_BUILD_DIR=
QEMU_CHANGES_FILE=
QEMU_DISABLE_KVM=0
QEMU_REQUESTED=0
REUSE_ARTIFACTS=0

log() {
  printf '[memory-installed-gate] %s\n' "$*"
}

fail() {
  printf '[memory-installed-gate] %s\n' "$*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Usage: bash scripts/packaging/validate_memory_installed_or_qemu.sh [options] [-- <autopkgtest-virt-server> [virt args...]]

Options:
  --evidence-root DIR  Override the evidence root.
                       Defaults to ~/.cache/dasall/memory/installed-evidence.
  --artifact-dir DIR   Override the package-smoke artifact directory used for
                       this run. Defaults to <evidence-root>/<timestamp>/package-smoke.
  --reuse-artifacts    Reuse existing run-first/run-second/memory proof artifacts
                       from --artifact-dir and only materialize installed-evidence.
                       Intended for focused regression tests, not as a substitute
                       for authoritative installed package smoke.
  --build-dir DIR      Forwarded to validate_gate_int_10_installed_package_qemu.sh
                       when a qemu/autopkgtest command is provided.
  --changes FILE       Forwarded to validate_gate_int_10_installed_package_qemu.sh
                       when a qemu/autopkgtest command is provided.
  --disable-kvm        Forwarded to validate_gate_int_10_installed_package_qemu.sh.
  -h, --help           Show this help text.

Environment:
  DASALL_MEMORY_INSTALLED_EVIDENCE_ROOT
                       Override the evidence root.
  DASALL_DEEPSEEK_API_KEY_FILE
                       Forwarded to pkg_smoke_install.sh when the installed
                       package smoke needs a host-side DeepSeek key.

Examples:
  bash scripts/packaging/validate_memory_installed_or_qemu.sh

  bash scripts/packaging/validate_memory_installed_or_qemu.sh \
    --disable-kvm \
    -- qemu --timeout-reboot=180 /path/to/autopkgtest.img
EOF
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || fail "missing required command: $1"
}

ensure_dir() {
  mkdir -p "$1"
}

require_file() {
  [ -f "$1" ] || fail "missing required artifact: $1"
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --evidence-root)
      [ "$#" -ge 2 ] || fail 'missing value for --evidence-root'
      EVIDENCE_ROOT=$2
      shift 2
      ;;
    --artifact-dir)
      [ "$#" -ge 2 ] || fail 'missing value for --artifact-dir'
      ARTIFACT_DIR=$2
      shift 2
      ;;
    --reuse-artifacts)
      REUSE_ARTIFACTS=1
      shift
      ;;
    --build-dir)
      [ "$#" -ge 2 ] || fail 'missing value for --build-dir'
      QEMU_BUILD_DIR=$2
      shift 2
      ;;
    --changes)
      [ "$#" -ge 2 ] || fail 'missing value for --changes'
      QEMU_CHANGES_FILE=$2
      shift 2
      ;;
    --disable-kvm)
      QEMU_DISABLE_KVM=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      QEMU_REQUESTED=1
      shift
      break
      ;;
    *)
      usage
      fail "unknown option: $1"
      ;;
  esac
done

if [ "$QEMU_REQUESTED" -eq 1 ] && [ "$#" -eq 0 ]; then
  usage
  fail 'missing autopkgtest virtualization command after --'
fi

require_command bash
require_command date
require_command mkdir
require_command python3

RUN_ID=$(date -u +%Y%m%dT%H%M%SZ)
RUN_DIR="${EVIDENCE_ROOT}/${RUN_ID}"
SUMMARY_PATH="${RUN_DIR}/summary.json"
LATEST_JSON_PATH="${EVIDENCE_ROOT}/latest.json"
PACKAGE_SMOKE_LOG_PATH="${RUN_DIR}/package-smoke.log"
QEMU_LOG_PATH="${RUN_DIR}/gate-int-10-qemu.log"

ensure_dir "$RUN_DIR"
ARTIFACT_DIR=${ARTIFACT_DIR:-${RUN_DIR}/package-smoke}
ensure_dir "$ARTIFACT_DIR"

if [ "$REUSE_ARTIFACTS" -eq 1 ]; then
  log "reusing existing local installed raw artifacts from ${ARTIFACT_DIR}"
  printf '%s\n' '[memory-installed-gate] reused existing package-smoke artifacts' \
    >"${PACKAGE_SMOKE_LOG_PATH}"
else
  log "running local installed memory proof via pkg_smoke_install.sh"
  if DASALL_PACKAGE_SMOKE_ARTIFACT_DIR="$ARTIFACT_DIR" \
    bash "${SCRIPT_DIR}/pkg_smoke_install.sh" --explicit-start-check \
    >"${PACKAGE_SMOKE_LOG_PATH}" 2>&1; then
    :
  else
    tail -n 80 "${PACKAGE_SMOKE_LOG_PATH}" >&2 || true
    fail "pkg_smoke_install.sh failed; see ${PACKAGE_SMOKE_LOG_PATH}"
  fi
fi

RUN_FIRST_JSON_PATH="${ARTIFACT_DIR}/run-first.json"
RUN_SECOND_JSON_PATH="${ARTIFACT_DIR}/run-second.json"
MEMORY_PROOF_JSON_PATH="${ARTIFACT_DIR}/memory-proof.json"
MEMORY_MAINTENANCE_JSON_PATH="${ARTIFACT_DIR}/memory-maintenance-proof.json"

require_file "$RUN_FIRST_JSON_PATH"
require_file "$RUN_SECOND_JSON_PATH"
require_file "$MEMORY_PROOF_JSON_PATH"
require_file "$MEMORY_MAINTENANCE_JSON_PATH"

QEMU_STATUS=not-requested
QEMU_RC=0
if [ "$QEMU_REQUESTED" -eq 1 ]; then
  log "running optional installed-package qemu gate via validate_gate_int_10_installed_package_qemu.sh"
  QEMU_STATUS=passed
  set -- -- "$@"
  if [ "$QEMU_DISABLE_KVM" -eq 1 ]; then
    set -- --disable-kvm "$@"
  fi
  if [ -n "$QEMU_CHANGES_FILE" ]; then
    set -- --changes "$QEMU_CHANGES_FILE" "$@"
  fi
  if [ -n "$QEMU_BUILD_DIR" ]; then
    set -- --build-dir "$QEMU_BUILD_DIR" "$@"
  fi

  if bash "${SCRIPT_DIR}/validate_gate_int_10_installed_package_qemu.sh" \
    "$@" >"${QEMU_LOG_PATH}" 2>&1; then
    :
  else
    QEMU_RC=$?
    QEMU_STATUS=failed
  fi
fi

python3 - \
  "$RUN_ID" \
  "$SUMMARY_PATH" \
  "$LATEST_JSON_PATH" \
  "$RUN_FIRST_JSON_PATH" \
  "$RUN_SECOND_JSON_PATH" \
  "$MEMORY_PROOF_JSON_PATH" \
  "$MEMORY_MAINTENANCE_JSON_PATH" \
  "$PACKAGE_SMOKE_LOG_PATH" \
  "$REUSE_ARTIFACTS" \
  "$QEMU_REQUESTED" \
  "$QEMU_STATUS" \
  "$QEMU_RC" \
  "$QEMU_LOG_PATH" <<'PY'
import json
import pathlib
import sys


def load_json(path: pathlib.Path) -> object:
    return json.loads(path.read_text(encoding="utf-8"))


run_id = sys.argv[1]
summary_path = pathlib.Path(sys.argv[2])
latest_json_path = pathlib.Path(sys.argv[3])
run_first_path = pathlib.Path(sys.argv[4])
run_second_path = pathlib.Path(sys.argv[5])
memory_proof_path = pathlib.Path(sys.argv[6])
maintenance_proof_path = pathlib.Path(sys.argv[7])
package_smoke_log_path = pathlib.Path(sys.argv[8])
reuse_artifacts = sys.argv[9] == "1"
qemu_requested = sys.argv[10] == "1"
qemu_status = sys.argv[11]
qemu_rc = int(sys.argv[12])
qemu_log_path = pathlib.Path(sys.argv[13])

run_first = load_json(run_first_path)
run_second = load_json(run_second_path)
memory_proof = load_json(memory_proof_path)
maintenance_proof = load_json(maintenance_proof_path)

run_first_text = json.dumps(run_first, ensure_ascii=True)
run_second_text = json.dumps(run_second, ensure_ascii=True)
latest_summary_sources = str(memory_proof.get("latest_summary_source_turn_ids_json", ""))
expected_marker = str(memory_proof.get("expected_marker", ""))
second_turn_id = str(memory_proof.get("second_turn_id", ""))
maintenance_report = maintenance_proof.get("maintenance_report", {})

checks = {
    "init": (
        run_first.get("disposition") == "completed"
        and run_first.get("task_completed") is True
        and bool(memory_proof.get("session_id"))
    ),
    "open_store": (
        memory_proof.get("journal_mode") == "wal"
        and int(memory_proof.get("core_table_count", 0)) >= 5
        and int(memory_proof.get("vector_table_count", 0)) >= 1
    ),
    "prepare_context": bool(expected_marker) and expected_marker in run_second_text,
    "write_back": (
        int(memory_proof.get("session_turn_count_after_second", 0)) >= 2
        and int(memory_proof.get("session_summary_count_after_second", 0)) >= 2
        and second_turn_id
        and second_turn_id in latest_summary_sources
    ),
    "maintenance": (
        maintenance_proof.get("ok") is True
        and maintenance_report.get("checkpoint_executed") is True
        and int(maintenance_report.get("checkpoint_wal_pages_remaining", -1)) == 0
        and int(maintenance_proof.get("quarantine_rows_after", -1)) == 0
    ),
}

if not all(checks.values()):
    raise SystemExit(
        "memory installed evidence validation failed: "
        + json.dumps(checks, ensure_ascii=True, sort_keys=True)
    )

summary = {
    "schema_version": 1,
    "run_id": run_id,
    "authoritative_owner": "scripts/packaging/validate_memory_installed_or_qemu.sh",
    "mode": "local-installed+qemu" if qemu_requested else "local-installed",
  "artifact_collection_mode": "reuse-artifacts" if reuse_artifacts else "pkg-smoke",
    "checks": checks,
    "effective_profile_id": maintenance_proof.get("effective_profile_id", ""),
    "local_installed": {
        "session_id": memory_proof.get("session_id", ""),
        "expected_marker": expected_marker,
        "journal_mode": memory_proof.get("journal_mode", ""),
        "core_table_count": int(memory_proof.get("core_table_count", 0)),
        "vector_table_count": int(memory_proof.get("vector_table_count", 0)),
        "first_turn_id": memory_proof.get("first_turn_id", ""),
        "second_turn_id": second_turn_id,
        "latest_summary_source_turn_ids_json": latest_summary_sources,
        "latest_summary_text_prefix": memory_proof.get("latest_summary_text_prefix", ""),
        "maintenance": {
            "turns_before": int(maintenance_proof.get("turns_before", 0)),
            "turns_after": int(maintenance_proof.get("turns_after", 0)),
            "retention_turns": int(maintenance_proof.get("retention_turns", 0)),
            "quarantine_rows_after": int(maintenance_proof.get("quarantine_rows_after", 0)),
            "checkpoint_executed": maintenance_report.get("checkpoint_executed", False),
            "checkpoint_wal_pages_remaining": int(
                maintenance_report.get("checkpoint_wal_pages_remaining", 0)
            ),
        },
    },
    "source_artifacts": {
        "run_first_json": str(run_first_path),
        "run_second_json": str(run_second_path),
        "memory_proof_json": str(memory_proof_path),
        "memory_maintenance_proof_json": str(maintenance_proof_path),
        "package_smoke_log": str(package_smoke_log_path),
    },
    "qemu": {
        "requested": qemu_requested,
        "status": qemu_status,
        "exit_code": qemu_rc,
        "gate_log": str(qemu_log_path) if qemu_requested else "",
    },
}

encoded = json.dumps(summary, indent=2, ensure_ascii=True) + "\n"
summary_path.write_text(encoded, encoding="ascii")
latest_json_path.write_text(encoded, encoding="ascii")
PY

rm -f "${EVIDENCE_ROOT}/latest"
ln -s "${RUN_ID}" "${EVIDENCE_ROOT}/latest"

log "wrote memory installed evidence to ${LATEST_JSON_PATH}"

if [ "$QEMU_RC" -ne 0 ]; then
  tail -n 80 "${QEMU_LOG_PATH}" >&2 || true
  fail "installed-package qemu gate failed; see ${QEMU_LOG_PATH}"
fi

cat "$LATEST_JSON_PATH"