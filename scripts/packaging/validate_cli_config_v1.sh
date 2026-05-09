#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "${SCRIPT_DIR}/../.." && pwd)
BUILD_DIR="${REPO_ROOT}/build/vscode-linux-ninja"
SECRET_TEST_PATTERN='^(SecretBootstrapWriterIntegrationTest|FileSecretBackendTest|SecretManagerFacadeTest)$'

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
require_file "${SCRIPT_DIR}/validate_ubuntu_dpkg_v1.sh"

log 'validating autopkgtest metadata'
(cd "${REPO_ROOT}" && autopkgtest --validate .)

log 'running build-tree secret onboarding regressions'
cmake --build --preset vscode-linux-ninja --target dasall_integration_tests
ctest --test-dir "${BUILD_DIR}" -R "${SECRET_TEST_PATTERN}" --output-on-failure

log 'running local installed-package lifecycle smoke'
sh "${SCRIPT_DIR}/validate_ubuntu_dpkg_v1.sh"

log 'CLI config validator passed'