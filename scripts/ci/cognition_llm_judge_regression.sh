#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/vscode-linux-ninja}"
ARTIFACT_DIR="${ARTIFACT_DIR:-${ROOT_DIR}/build/cognition-judge-regression-artifacts}"
REPLAY_DIR="${REPLAY_DIR:-${ROOT_DIR}/tests/data/cognition/replay}"
FAILURE_SAMPLES_DIR="${FAILURE_SAMPLES_DIR:-}"
PROMPT_RELEASE_ID="${PROMPT_RELEASE_ID:-}"
TEST_BINARY="${BUILD_DIR}/tests/integration/cognition/dasall_cognition_llm_judge_regression_integration_test"

if [[ ! -d "${REPLAY_DIR}" ]]; then
  echo "[cognition_llm_judge_regression] missing replay dir: ${REPLAY_DIR}" >&2
  exit 1
fi

if [[ ! -x "${TEST_BINARY}" ]]; then
  echo "[cognition_llm_judge_regression] missing test binary: ${TEST_BINARY}" >&2
  echo "[cognition_llm_judge_regression] build target dasall_cognition_llm_judge_regression_integration_test before running this script" >&2
  exit 2
fi

rm -rf "${ARTIFACT_DIR}"
mkdir -p "${ARTIFACT_DIR}"

export DASALL_COGNITION_JUDGE_ARTIFACT_DIR="${ARTIFACT_DIR}"
export DASALL_COGNITION_JUDGE_REPLAY_DIR="${REPLAY_DIR}"

if [[ -n "${FAILURE_SAMPLES_DIR}" ]]; then
  export DASALL_COGNITION_JUDGE_FAILURE_SAMPLES_DIR="${FAILURE_SAMPLES_DIR}"
else
  unset DASALL_COGNITION_JUDGE_FAILURE_SAMPLES_DIR 2>/dev/null || true
fi

if [[ -n "${PROMPT_RELEASE_ID}" ]]; then
  export DASALL_COGNITION_JUDGE_PROMPT_RELEASE_ID="${PROMPT_RELEASE_ID}"
else
  unset DASALL_COGNITION_JUDGE_PROMPT_RELEASE_ID 2>/dev/null || true
fi

"${TEST_BINARY}"

echo "[cognition_llm_judge_regression] report: ${ARTIFACT_DIR}/judge-report.md"
cat "${ARTIFACT_DIR}/judge-report.md"