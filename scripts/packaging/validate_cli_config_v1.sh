#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "${SCRIPT_DIR}/../.." && pwd)
BUILD_DIR="${REPO_ROOT}/build/vscode-linux-ninja"
SECRET_TEST_PATTERN='^(SecretBootstrapWriterIntegrationTest|FileSecretBackendTest|SecretManagerFacadeTest)$'
SECRET_TEST_TARGETS='dasall_secret_bootstrap_writer_integration_test dasall_file_secret_backend_unit_test dasall_secret_manager_facade_unit_test'

log() {
  printf '[validate-cli-config-v1] %s\n' "$*"
}

fail() {
  printf '[validate-cli-config-v1] %s\n' "$*" >&2
  exit 1
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || fail "missing required command: $1"
}

require_file() {
  [ -f "$1" ] || fail "missing required file: $1"
}

require_command autopkgtest
require_command cmake
require_command ctest
require_command python3
require_file "${SCRIPT_DIR}/validate_ubuntu_dpkg_v1.sh"
require_file "${SCRIPT_DIR}/validate_autopkgtest_metadata.py"

log 'validating autopkgtest metadata'
(python3 "${SCRIPT_DIR}/validate_autopkgtest_metadata.py")

log 'running build-tree secret onboarding regressions'
cmake --build --preset vscode-linux-ninja --target ${SECRET_TEST_TARGETS}
ctest --test-dir "${BUILD_DIR}" -R "${SECRET_TEST_PATTERN}" --output-on-failure

log 'running local installed-package lifecycle smoke'
sh "${SCRIPT_DIR}/validate_ubuntu_dpkg_v1.sh"

log 'CLI config validator passed'