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

require_command cmake
require_command dpkg-buildpackage
require_command python3
require_command autopkgtest

[ -d "${BUILD_DIR}" ] || fail "missing CMake build directory: ${BUILD_DIR}"

cd "${REPO_ROOT}"

log 'running build-tree Gate-INT-10 release/app-binary preflight'
cmake --build "${BUILD_DIR}" --target dasall_gate_int_10
cmake --build "${BUILD_DIR}" --target dasall_packaging_preflight_tests

log 'building Debian package artifacts'
dpkg-buildpackage -us -uc -b

[ -f "${CHANGES_FILE}" ] || fail "missing .changes artifact after package build: ${CHANGES_FILE}"

log 'validating autopkgtest metadata'
python3 "${SCRIPT_DIR}/validate_autopkgtest_metadata.py"

log "running authoritative installed-package autopkgtest: ${CHANGES_FILE}"
if [ "${DISABLE_KVM}" -eq 1 ]; then
  AUTOPKGTEST_QEMU_DISABLE_KVM=1 autopkgtest "${CHANGES_FILE}" -- "$@"
else
  autopkgtest "${CHANGES_FILE}" -- "$@"
fi

log 'Gate-INT-10 handoff and installed-package qemu autopkgtest gate passed'