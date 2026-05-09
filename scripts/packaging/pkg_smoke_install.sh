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

CONFIG_SMOKE_DIR=/tmp/dasall-cli-config-smoke
CONFIG_IMPORT_FILE="${CONFIG_SMOKE_DIR}/deepseek.key"
CONFIG_DESIRED_FILE="${CONFIG_SMOKE_DIR}/desired.yaml"
CONFIG_SHOW_JSON="${CONFIG_SMOKE_DIR}/show-before.json"
CONFIG_VALIDATE_JSON="${CONFIG_SMOKE_DIR}/validate.json"
CONFIG_PLAN_JSON="${CONFIG_SMOKE_DIR}/plan.json"
CONFIG_APPLY_JSON="${CONFIG_SMOKE_DIR}/apply.json"
CONFIG_AFTER_JSON="${CONFIG_SMOKE_DIR}/show-after.json"

log() {
  printf '[pkg-smoke-install] %s\n' "$*"
}

fail() {
  printf '[pkg-smoke-install] %s\n' "$*" >&2
  exit 1
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || fail "missing required command: $1"
}

require_artifact() {
  [ -f "$1" ] || fail "missing package artifact: $1"
}

require_root_access() {
  if [ "$(id -u)" -eq 0 ]; then
    return 0
  fi

  command -v sudo >/dev/null 2>&1 || fail 'root or sudo is required'

  if [ "${DASALL_ALLOW_INTERACTIVE_SUDO:-0}" = "1" ]; then
    sudo -v >/dev/null 2>&1 || fail 'sudo validation failed'
    return 0
  fi

  sudo -n true >/dev/null 2>&1 || fail 'passwordless sudo is required for automated lifecycle smoke; rerun as root or set DASALL_ALLOW_INTERACTIVE_SUDO=1'
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

cleanup() {
  run_root systemctl disable --now dasall-daemon.service >/dev/null 2>&1 || true
  run_root rm -rf "${CONFIG_SMOKE_DIR}" >/dev/null 2>&1 || true
}

trap cleanup EXIT

reset_existing_state() {
  log 'resetting previous DASALL install state'
  run_root_sh '
    systemctl disable --now dasall-daemon.service >/dev/null 2>&1 || true
    dpkg -P dasall dasall-daemon dasall-cli dasall-common >/dev/null 2>&1 || true
    rm -f /var/lib/dasall/pkg-smoke-state
    rm -rf /var/lib/dasall/secrets
    rm -rf /tmp/dasall-cli-config-smoke
  '
}

install_packages() {
  log 'installing Debian package set'
  run_root dpkg -i "$COMMON_DEB" "$CLI_DEB" "$DAEMON_DEB" "$META_DEB"
}

verify_packages_installed() {
  for pkg in dasall dasall-cli dasall-daemon dasall-common; do
    run_root_sh "dpkg-query -W -f='\${Status}\n' ${pkg} | grep -Fqx 'install ok installed'"
  done
}

verify_service_not_started() {
  run_root_sh '! systemctl is-enabled --quiet dasall-daemon.service >/dev/null 2>&1'
  run_root_sh '! systemctl is-active --quiet dasall-daemon.service >/dev/null 2>&1'
}

verify_installed_files() {
  run_root test -f /usr/share/doc/dasall-daemon/README.Debian
  run_root test -f /etc/default/dasall-daemon
  run_root test -f /etc/dasall/daemon.json
}

verify_validate_only() {
  log 'verifying daemon validate-only against installed defaults'
  run_root_sh '. /etc/default/dasall-daemon && : "${DASALL_DAEMON_PROFILE_ID:?missing DASALL_DAEMON_PROFILE_ID}" && /usr/sbin/dasall-daemon --validate-only --profile-id="${DASALL_DAEMON_PROFILE_ID}" --config-file /etc/dasall/daemon.json'
}

prepare_config_smoke_dir() {
  run_root_sh "rm -rf '${CONFIG_SMOKE_DIR}' && mkdir -p '${CONFIG_SMOKE_DIR}' && chmod 700 '${CONFIG_SMOKE_DIR}'"
}

verify_config_show_and_validate() {
  log 'verifying installed dasall config show/validate commands'
  prepare_config_smoke_dir
  run_root_sh "dasall config show --json > '${CONFIG_SHOW_JSON}'"
  run_root_sh "grep -Fq '\"schema_version\":\"dasall.config.summary.v1\"' '${CONFIG_SHOW_JSON}'"
  run_root_sh "dasall config validate --json > '${CONFIG_VALIDATE_JSON}'"
  run_root_sh "grep -Fq '\"schema_version\":\"dasall.config.validate.v1\"' '${CONFIG_VALIDATE_JSON}'"
}

prepare_config_apply_fixture() {
  log 'preparing config apply secret import fixture'
  prepare_config_smoke_dir
  run_root_sh ". /etc/default/dasall-daemon
: \"\${DASALL_DAEMON_PROFILE_ID:?missing DASALL_DAEMON_PROFILE_ID}\"
cat <<'EOF' > '${CONFIG_IMPORT_FILE}'
deepseek-file-secret
EOF
chmod 600 '${CONFIG_IMPORT_FILE}'
cat <<EOF > '${CONFIG_DESIRED_FILE}'
schema_version: dasall.config.apply.v1
profile_id: \${DASALL_DAEMON_PROFILE_ID}
daemon:
  socket_path: /run/dasall/daemon.sock
  log_format: text
  diag_enabled: true
  override_enabled: false
  watchdog_enabled: false
service:
  start_now: true
  enable_on_boot: true
operator_access:
  add_users: []
secrets:
  refs:
    - ref: secret://llm/providers/deepseek-prod
      source: file:${CONFIG_IMPORT_FILE}
      auth_profile_name: primary
EOF
chmod 600 '${CONFIG_DESIRED_FILE}'"
}

verify_explicit_start() {
  log 'verifying daemon stays stopped until explicit config apply'
  run_root_sh '! systemctl is-enabled --quiet dasall-daemon.service >/dev/null 2>&1'
  run_root_sh '! systemctl is-active --quiet dasall-daemon.service >/dev/null 2>&1'

  prepare_config_apply_fixture

  run_root_sh "dasall config plan --from-file '${CONFIG_DESIRED_FILE}' --json > '${CONFIG_PLAN_JSON}'"
  run_root_sh "grep -Fq '\"schema_version\":\"dasall.config.plan.v1\"' '${CONFIG_PLAN_JSON}'"
  run_root_sh "grep -Fq '\"ref\":\"secret://llm/providers/deepseek-prod\"' '${CONFIG_PLAN_JSON}'"
  run_root_sh "grep -Fq '\"service_start_requested\":true' '${CONFIG_PLAN_JSON}'"

  run_root_sh "dasall config apply --from-file '${CONFIG_DESIRED_FILE}' --no-input --json > '${CONFIG_APPLY_JSON}'"
  run_root_sh "grep -Fq '\"outcome\":\"applied\"' '${CONFIG_APPLY_JSON}'"
  run_root_sh "grep -Fq '\"written_secret_refs\":[\"secret://llm/providers/deepseek-prod\"]' '${CONFIG_APPLY_JSON}'"

  run_root_sh "grep -Fq '\"log_format\": \"text\"' /etc/dasall/daemon.json"
  run_root_sh "grep -Fq '\"diag_enabled\": true' /etc/dasall/daemon.json"
  run_root test -f /var/lib/dasall/secrets/llm/providers/deepseek-prod.secret
  run_root_sh "! grep -Fq 'deepseek-file-secret' /var/lib/dasall/secrets/llm/providers/deepseek-prod.secret"

  run_root_sh "dasall config show --json > '${CONFIG_AFTER_JSON}'"
  run_root_sh "grep -Fq '\"ref\":\"secret://llm/providers/deepseek-prod\"' '${CONFIG_AFTER_JSON}'"
  run_root_sh "grep -Fq '\"status\":\"configured\"' '${CONFIG_AFTER_JSON}'"
  run_root_sh "grep -Fq '\"running\":true' '${CONFIG_AFTER_JSON}'"
  run_root_sh "grep -Fq '\"enabled\":true' '${CONFIG_AFTER_JSON}'"

  run_root systemctl is-active --quiet dasall-daemon.service
  run_root test -S /run/dasall/daemon.sock
  run_root_sh 'test "$(stat -c "%U:%G" /run/dasall/daemon.sock)" = "dasall:dasall"'
  run_root_sh 'test "$(stat -c "%a" /run/dasall/daemon.sock)" = "600"'
  run_root dasall ping --json >/dev/null
  run_root dasall readiness --json >/dev/null
  run_root dasall version >/dev/null
}

usage() {
  cat <<'EOF'
Usage: bash scripts/packaging/pkg_smoke_install.sh [--explicit-start-check]
EOF
}

EXPLICIT_START_CHECK=0

case "${1:-}" in
  "")
    ;;
  --explicit-start-check)
    EXPLICIT_START_CHECK=1
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

require_command dpkg
require_command dpkg-parsechangelog
require_command systemctl
require_artifact "$COMMON_DEB"
require_artifact "$CLI_DEB"
require_artifact "$DAEMON_DEB"
require_artifact "$META_DEB"

reset_existing_state
install_packages
verify_packages_installed
verify_service_not_started
verify_installed_files
verify_validate_only
verify_config_show_and_validate

if [ "$EXPLICIT_START_CHECK" -eq 1 ]; then
  verify_explicit_start
fi

log 'install smoke passed'