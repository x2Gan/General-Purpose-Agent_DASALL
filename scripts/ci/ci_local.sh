#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"

"${ROOT_DIR}/scripts/ci/build.sh"
"${ROOT_DIR}/scripts/ci/unit_tests.sh"
"${ROOT_DIR}/scripts/ci/contract_tests.sh"
"${ROOT_DIR}/scripts/ci/static_check.sh"
