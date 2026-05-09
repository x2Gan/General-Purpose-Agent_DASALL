#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
STATE_SENTINEL=/var/lib/dasall/pkg-smoke-state

log() {
  printf '[pkg-smoke-remove-purge] %s\n' "$*"
}

fail() {
  printf '[pkg-smoke-remove-purge] %s\n' "$*" >&2
  exit 1
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

prepare_installed_state() {
  if [ "$ASSUME_INSTALLED" -eq 1 ]; then
    return 0
  fi

  log 'preparing baseline install state'
  "${SCRIPT_DIR}/pkg_smoke_install.sh" --explicit-start-check
}

prepare_runtime_state() {
  log 'creating runtime state sentinel'
  run_root mkdir -p /var/lib/dasall
  run_root_sh "printf '%s\n' 'pkg-smoke-state' > ${STATE_SENTINEL}"
}

perform_remove() {
  log 'removing Debian packages'
  run_root dpkg -r dasall dasall-daemon dasall-cli dasall-common
}

verify_remove_outcome() {
  run_root test -f /etc/dasall/daemon.json
  run_root test -f "${STATE_SENTINEL}"
  run_root_sh '! systemctl is-active --quiet dasall-daemon.service >/dev/null 2>&1'
  run_root_sh '! dpkg-query -W -f="\${Status}\n" dasall-daemon | grep -Fqx "install ok installed"'
}

perform_purge() {
  log 'purging Debian packages'
  run_root dpkg -P dasall dasall-daemon dasall-cli dasall-common
}

verify_purge_outcome() {
  run_root_sh '! test -f /etc/dasall/daemon.json'
  run_root test -f "${STATE_SENTINEL}"
  for pkg in dasall dasall-daemon dasall-cli dasall-common; do
    run_root_sh "! dpkg-query -W -f='\${Status}\n' ${pkg} 2>/dev/null | grep -Fqx 'install ok installed'"
  done
}

usage() {
  cat <<'EOF'
Usage: bash scripts/packaging/pkg_smoke_remove_purge.sh [--assume-installed]
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

prepare_installed_state
prepare_runtime_state
perform_remove
verify_remove_outcome
perform_purge
verify_purge_outcome

log 'remove/purge smoke passed'