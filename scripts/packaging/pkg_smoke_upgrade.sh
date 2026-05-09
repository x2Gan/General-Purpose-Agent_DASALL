#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "${SCRIPT_DIR}/../.." && pwd)
ARTIFACT_DIR=$(CDPATH= cd -- "${REPO_ROOT}/.." && pwd)
ARCH=$(dpkg --print-architecture)
VERSION=$(cd "${REPO_ROOT}" && dpkg-parsechangelog -SVersion)

COMMON_DEB="${ARTIFACT_DIR}/dasall-common_${VERSION}_all.deb"
CLI_DEB="${ARTIFACT_DIR}/dasall-cli_${VERSION}_${ARCH}.deb"
DAEMON_DEB="${ARTIFACT_DIR}/dasall-daemon_${VERSION}_${ARCH}.deb"
META_DEB="${ARTIFACT_DIR}/dasall_${VERSION}_all.deb"

log() {
  printf '[pkg-smoke-upgrade] %s\n' "$*"
}

fail() {
  printf '[pkg-smoke-upgrade] %s\n' "$*" >&2
  exit 1
}

require_artifact() {
  [ -f "$1" ] || fail "missing package artifact: $1"
}

require_root_access() {
  if [ "$(id -u)" -eq 0 ]; then
    return 0
  fi

  command -v sudo >/dev/null 2>&1 || fail "root or sudo is required"

  if [ "${DASALL_ALLOW_INTERACTIVE_SUDO:-0}" = "1" ]; then
    sudo -v >/dev/null 2>&1 || fail "sudo validation failed"
    return 0
  fi

  sudo -n true >/dev/null 2>&1 || fail "passwordless sudo is required for automated lifecycle smoke; rerun as root or set DASALL_ALLOW_INTERACTIVE_SUDO=1"
}

run_root() {
  require_root_access
  if [ "$(id -u)" -eq 0 ]; then
    "$@"
  else
    sudo "$@"
  fi
}

run_root_sh() {
  require_root_access
  if [ "$(id -u)" -eq 0 ]; then
    sh -eu -c "$1"
  else
    sudo sh -eu -c "$1"
  fi
}

verify_packages_installed() {
  for pkg in dasall dasall-cli dasall-daemon dasall-common; do
    run_root_sh "dpkg-query -W -f='\${Status}\n' ${pkg} | grep -Fqx 'install ok installed'"
  done
}

prepare_installed_state() {
  if [ "$ASSUME_INSTALLED" -eq 1 ]; then
    return 0
  fi

  log 'preparing baseline install state'
  "${SCRIPT_DIR}/pkg_smoke_install.sh" --explicit-start-check
}

prepare_operator_change() {
  log 'recording operator-owned conffile change'
  run_root_sh "sed -i 's/\"diag_enabled\": false/\"diag_enabled\": true/' /etc/dasall/daemon.json"
  run_root_sh "grep -Fq '\"diag_enabled\": true' /etc/dasall/daemon.json"
}

perform_upgrade() {
  log 'stopping daemon before upgrade to assert no surprise auto-start'
  run_root systemctl disable --now dasall-daemon.service >/dev/null 2>&1 || true
  log 'reinstalling Debian package set to exercise upgrade path'
  run_root dpkg -i "$COMMON_DEB" "$CLI_DEB" "$DAEMON_DEB" "$META_DEB"
}

verify_upgrade_outcome() {
  verify_packages_installed
  run_root_sh "grep -Fq '\"diag_enabled\": true' /etc/dasall/daemon.json"
  run_root_sh '! systemctl is-enabled --quiet dasall-daemon.service >/dev/null 2>&1'
  run_root_sh '! systemctl is-active --quiet dasall-daemon.service >/dev/null 2>&1'
  run_root_sh '. /etc/default/dasall-daemon && /usr/sbin/dasall-daemon --validate-only --profile-id="${DASALL_DAEMON_PROFILE_ID}" --config-file /etc/dasall/daemon.json'
}

usage() {
  cat <<'EOF'
Usage: bash scripts/packaging/pkg_smoke_upgrade.sh [--assume-installed]
EOF
}

ASSUME_INSTALLED=0

case "${1:-}" in
  "")
    ;;
  --assume-installed)
    ASSUME_INSTALLED=1
    shift
    ;;
  -h|--help)
    usage
    exit 0
    ;;
  *)
    usage
    fail "unknown option: ${1}"
    ;;
esac

[ "$#" -eq 0 ] || fail 'unexpected positional arguments'

require_artifact "$COMMON_DEB"
require_artifact "$CLI_DEB"
require_artifact "$DAEMON_DEB"
require_artifact "$META_DEB"

prepare_installed_state
prepare_operator_change
perform_upgrade
verify_upgrade_outcome

log 'upgrade smoke passed'