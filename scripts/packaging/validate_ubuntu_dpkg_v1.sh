#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)

log() {
  printf '[validate-ubuntu-dpkg-v1] %s\n' "$*"
}

log 'running fresh-install and explicit-start smoke'
"${SCRIPT_DIR}/pkg_smoke_install.sh" --explicit-start-check

log 'running upgrade smoke'
"${SCRIPT_DIR}/pkg_smoke_upgrade.sh" --assume-installed

log 'running remove/purge smoke'
"${SCRIPT_DIR}/pkg_smoke_remove_purge.sh" --assume-installed

log 'Ubuntu DPKG v1 lifecycle smoke passed'