#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"

if command -v cppcheck >/dev/null 2>&1; then
	export DASALL_ENABLE_CPPCHECK="${DASALL_ENABLE_CPPCHECK:-ON}"
fi

if command -v clang-tidy >/dev/null 2>&1; then
	export DASALL_ENABLE_CLANG_TIDY="${DASALL_ENABLE_CLANG_TIDY:-ON}"
fi

"${ROOT_DIR}/scripts/ci/build.sh"
"${ROOT_DIR}/scripts/ci/unit_tests.sh"
"${ROOT_DIR}/scripts/ci/contract_tests.sh"
"${ROOT_DIR}/scripts/ci/static_check.sh"
