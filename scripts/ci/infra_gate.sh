#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-ci}"
ALLOW_BLOCKED="${ALLOW_BLOCKED:-0}"

# ARC-02: 统一执行 Blocked 先解阻门禁。
todo_pattern='DASALL_infrastructure.*专项TODO\.md|DASALL_profiles模块专项TODO\.md|DASALL_platform_linux模块专项TODO\.md'
mapfile -t TODO_FILES < <(rg --files "${ROOT_DIR}/docs/todos" | rg "${todo_pattern}")

if [[ "${#TODO_FILES[@]}" -eq 0 ]]; then
  echo "[infra_gate] no infra-related TODO files found"
  exit 1
fi

echo "[infra_gate] checking TODO three-piece columns"
for file in "${TODO_FILES[@]}"; do
  if ! rg -q "代码目标" "${file}" || ! rg -q "测试目标" "${file}" || ! rg -q "验收命令" "${file}"; then
    echo "[infra_gate] missing three-piece columns: ${file}"
    exit 1
  fi
done

blocked_count="$(rg -n "\|\s*Blocked\s*\|" "${TODO_FILES[@]}" | wc -l | tr -d ' ')"
echo "[infra_gate] blocked items: ${blocked_count}"

if [[ "${blocked_count}" -gt 0 && "${ALLOW_BLOCKED}" != "1" ]]; then
  echo "[infra_gate] failed: blocked-first gate is enabled (set ALLOW_BLOCKED=1 only for approved unblock window)"
  exit 2
fi

if [[ -f "${BUILD_DIR}/CTestTestfile.cmake" ]]; then
  echo "[infra_gate] checking ctest discoverability"
  ctest --test-dir "${BUILD_DIR}" -N >/tmp/infra_gate_ctest.list
  if ! rg -q "integration" /tmp/infra_gate_ctest.list; then
    echo "[infra_gate] failed: integration tests are not discoverable in ${BUILD_DIR}"
    exit 3
  fi
else
  echo "[infra_gate] warning: ${BUILD_DIR}/CTestTestfile.cmake not found, skip ctest discoverability"
fi

echo "[infra_gate] pass"
