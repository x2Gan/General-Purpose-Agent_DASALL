#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "${SCRIPT_DIR}/../.." && pwd)
ARTIFACT_DIR=$(CDPATH= cd -- "${REPO_ROOT}/.." && pwd)
ARCH=$(dpkg --print-architecture)
VERSION=$(cd "${REPO_ROOT}" && dpkg-parsechangelog -SVersion)

BUILD_DIR=${DASALL_BUILD_DIR:-build/vscode-linux-ninja}
CHANGES_FILE=${DASALL_CHANGES_FILE:-${ARTIFACT_DIR}/dasall_${VERSION}_${ARCH}.changes}
DISABLE_KVM=0
AUTOPKGTEST_SETUP_COMMANDS=${DASALL_AUTOPKGTEST_SETUP_COMMANDS:-}
AUTOPKGTEST_SETUP_COMMANDS_BOOT=${DASALL_AUTOPKGTEST_SETUP_COMMANDS_BOOT:-}
AUTOPKGTEST_TESTBED_SECRET_PATH=${DASALL_AUTOPKGTEST_TESTBED_SECRET_PATH:-/tmp/dasall-release/deepseek.key}
AUTOPKGTEST_OUTPUT_DIR=${DASALL_AUTOPKGTEST_OUTPUT_DIR:-}

log() {
  printf '[gate-int-10-installed-package-qemu] %s\n' "$*"
}

fail() {
  printf '[gate-int-10-installed-package-qemu] %s\n' "$*" >&2
  exit 1
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || fail "missing required command: $1"
}

usage() {
  cat <<'EOF'
Usage: sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh [options] -- <autopkgtest-virt-server> [virt args...]

Options:
  --build-dir DIR       CMake build directory for Gate-INT-10 targets.
                        Defaults to $DASALL_BUILD_DIR or build/vscode-linux-ninja.
  --changes FILE        .changes artifact to pass to autopkgtest.
                        Defaults to ../dasall_<version>_<arch>.changes.
  --disable-kvm         Export AUTOPKGTEST_QEMU_DISABLE_KVM=1 for non-KVM hosts.
  -h, --help            Show this help text.

Environment:
  DASALL_DEEPSEEK_API_KEY_FILE       Optional host-side DeepSeek key file.
                                     When set, the script copies it into the
                                     testbed and exposes the same variable to
                                     installed-package smoke.
  DASALL_AUTOPKGTEST_TESTBED_SECRET_PATH
                                     Testbed path for the copied DeepSeek key.
                                     Defaults to /tmp/dasall-release/deepseek.key.
  DASALL_AUTOPKGTEST_SETUP_COMMANDS  Optional autopkgtest --setup-commands
                                     value for release-runner preflight.
  DASALL_AUTOPKGTEST_SETUP_COMMANDS_BOOT
                                     Optional autopkgtest --setup-commands-boot
                                     value for per-boot preflight.
  DASALL_AUTOPKGTEST_OUTPUT_DIR       Optional autopkgtest --output-dir value.
                                     When set, autopkgtest artifacts are written
                                     to this directory for later archive/upload.

Examples:
  sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh \
    --disable-kvm -- qemu --timeout-reboot=180 /path/to/autopkgtest.img

  sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh \
    -- /usr/bin/autopkgtest-virt-qemu --timeout-reboot=180 /path/to/autopkgtest.img
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --build-dir)
      [ "$#" -ge 2 ] || fail 'missing value for --build-dir'
      BUILD_DIR=$2
      shift 2
      ;;
    --changes)
      [ "$#" -ge 2 ] || fail 'missing value for --changes'
      CHANGES_FILE=$2
      shift 2
      ;;
    --disable-kvm)
      DISABLE_KVM=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    *)
      usage
      fail "unknown option: $1"
      ;;
  esac
done

[ "$#" -gt 0 ] || {
  usage
  fail 'missing autopkgtest virtualization command after --'
}

case "${BUILD_DIR}" in
  /*) ;;
  *) BUILD_DIR="${REPO_ROOT}/${BUILD_DIR}" ;;
esac

case "${CHANGES_FILE}" in
  /*) ;;
  *) CHANGES_FILE="${REPO_ROOT}/${CHANGES_FILE}" ;;
esac

case "${AUTOPKGTEST_OUTPUT_DIR}" in
  '') ;;
  /*) ;;
  *) AUTOPKGTEST_OUTPUT_DIR="${REPO_ROOT}/${AUTOPKGTEST_OUTPUT_DIR}" ;;
esac

require_command cmake
require_command dpkg-buildpackage
require_command python3
require_command autopkgtest

[ -d "${BUILD_DIR}" ] || fail "missing CMake build directory: ${BUILD_DIR}"

if [ -n "${DASALL_DEEPSEEK_API_KEY_FILE:-}" ]; then
  [ -f "${DASALL_DEEPSEEK_API_KEY_FILE}" ] ||
    fail "missing DeepSeek key file: ${DASALL_DEEPSEEK_API_KEY_FILE}"
fi

cd "${REPO_ROOT}"

log 'running build-tree Gate-INT-10 release/app-binary preflight'
cmake --build "${BUILD_DIR}" --target dasall_gate_int_10
cmake --build "${BUILD_DIR}" --target dasall_packaging_preflight_tests

log 'building Debian package artifacts'
dpkg-buildpackage -us -uc -b

[ -f "${CHANGES_FILE}" ] || fail "missing .changes artifact after package build: ${CHANGES_FILE}"

log 'validating autopkgtest metadata'
python3 "${SCRIPT_DIR}/validate_autopkgtest_metadata.py"

set -- "${CHANGES_FILE}" -- "$@"

if [ -n "${AUTOPKGTEST_OUTPUT_DIR}" ]; then
  mkdir -p "${AUTOPKGTEST_OUTPUT_DIR}"
  log "forwarding autopkgtest output-dir archive: ${AUTOPKGTEST_OUTPUT_DIR}"
  set -- --output-dir "${AUTOPKGTEST_OUTPUT_DIR}" "$@"
fi

if [ -n "${AUTOPKGTEST_SETUP_COMMANDS_BOOT}" ]; then
  log 'forwarding autopkgtest setup-commands-boot preflight'
  set -- --setup-commands-boot "${AUTOPKGTEST_SETUP_COMMANDS_BOOT}" "$@"
fi

if [ -n "${AUTOPKGTEST_SETUP_COMMANDS}" ]; then
  log 'forwarding autopkgtest setup-commands preflight'
  set -- --setup-commands "${AUTOPKGTEST_SETUP_COMMANDS}" "$@"
fi

if [ -n "${DASALL_DEEPSEEK_API_KEY_FILE:-}" ]; then
  log "injecting DeepSeek key into testbed: ${AUTOPKGTEST_TESTBED_SECRET_PATH}"
  set -- \
    --env "DASALL_DEEPSEEK_API_KEY_FILE=${AUTOPKGTEST_TESTBED_SECRET_PATH}" \
    --copy "${DASALL_DEEPSEEK_API_KEY_FILE}:${AUTOPKGTEST_TESTBED_SECRET_PATH}" \
    "$@"
fi

log "running authoritative installed-package autopkgtest: ${CHANGES_FILE}"
if [ "${DISABLE_KVM}" -eq 1 ]; then
  AUTOPKGTEST_QEMU_DISABLE_KVM=1 autopkgtest "$@"
else
  autopkgtest "$@"
fi

log 'Gate-INT-10 handoff and installed-package qemu autopkgtest gate passed'