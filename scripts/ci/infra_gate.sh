#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-ci}"
ALLOW_BLOCKED="${ALLOW_BLOCKED:-0}"
CTEST_BIN="${CTEST_BIN:-ctest}"

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT

test_labels=(unit contract integration failure)

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
  echo "[infra_gate] running classified test gate"

  failed_labels=()
  skipped_labels=()

  for label in "${test_labels[@]}"; do
    discover_file="${TMP_DIR}/${label}.discover.txt"
    if ! "${CTEST_BIN}" --test-dir "${BUILD_DIR}" -N -L "${label}" >"${discover_file}"; then
      echo "[infra_gate] failed: could not discover ${label} tests in ${BUILD_DIR}"
      exit 3
    fi

    discovered_count="$(sed -n 's/^Total Tests: \([0-9][0-9]*\)$/\1/p' "${discover_file}" | tail -n 1)"
    discovered_count="${discovered_count:-0}"

    if [[ "${discovered_count}" == "0" ]]; then
      echo "[infra_gate] ${label}: skipped (0 discovered)"
      skipped_labels+=("${label}")
      continue
    fi

    echo "[infra_gate] ${label}: discovered ${discovered_count}, executing"
    if ! "${CTEST_BIN}" --test-dir "${BUILD_DIR}" --output-on-failure -L "${label}"; then
      echo "[infra_gate] ${label}: failed"
      failed_labels+=("${label}")
      continue
    fi

    echo "[infra_gate] ${label}: passed"
  done

  if [[ "${#failed_labels[@]}" -gt 0 ]]; then
    echo "[infra_gate] failed categories: ${failed_labels[*]}"
    exit 4
  fi

  if [[ "${#skipped_labels[@]}" -gt 0 ]]; then
    echo "[infra_gate] skipped categories: ${skipped_labels[*]}"
  fi
else
  echo "[infra_gate] warning: ${BUILD_DIR}/CTestTestfile.cmake not found, skip classified ctest execution"
fi

echo "[infra_gate] pass"
