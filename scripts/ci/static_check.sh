#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-ci}"

run_cppcheck() {
  if ! command -v cppcheck >/dev/null 2>&1; then
    echo "[static-check] cppcheck not found, skip"
    return 0
  fi

  cppcheck \
    --enable=warning,performance,portability,style \
    --error-exitcode=1 \
    --quiet \
    "${ROOT_DIR}/apps" \
    "${ROOT_DIR}/runtime" \
    "${ROOT_DIR}/cognition" \
    "${ROOT_DIR}/llm" \
    "${ROOT_DIR}/tools" \
    "${ROOT_DIR}/memory" \
    "${ROOT_DIR}/knowledge" \
    "${ROOT_DIR}/services" \
    "${ROOT_DIR}/multi_agent" \
    "${ROOT_DIR}/platform" \
    "${ROOT_DIR}/infra"
}

run_clang_tidy() {
  if ! command -v clang-tidy >/dev/null 2>&1; then
    echo "[static-check] clang-tidy not found, skip"
    return 0
  fi

  if [[ ! -f "${BUILD_DIR}/compile_commands.json" ]]; then
    echo "[static-check] compile_commands.json missing, run build.sh first"
    return 1
  fi

  local files
  mapfile -t files < <(find \
    "${ROOT_DIR}/apps" \
    "${ROOT_DIR}/runtime" \
    "${ROOT_DIR}/cognition" \
    "${ROOT_DIR}/llm" \
    "${ROOT_DIR}/tools" \
    "${ROOT_DIR}/memory" \
    "${ROOT_DIR}/knowledge" \
    "${ROOT_DIR}/services" \
    "${ROOT_DIR}/multi_agent" \
    "${ROOT_DIR}/platform" \
    "${ROOT_DIR}/infra" \
    \( -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' \))

  if [[ ${#files[@]} -eq 0 ]]; then
    echo "[static-check] no C++ files found"
    return 0
  fi

  clang-tidy -p "${BUILD_DIR}" "${files[@]}"
}

run_cppcheck
run_clang_tidy
