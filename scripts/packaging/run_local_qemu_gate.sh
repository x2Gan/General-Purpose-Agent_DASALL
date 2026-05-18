#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "${SCRIPT_DIR}/../.." && pwd)
SELF=$(CDPATH= cd -- "$(dirname "$0")" && pwd)/$(basename "$0")

QEMU_ENV_FILE=${DASALL_LOCAL_QEMU_ENV_FILE:-$HOME/.local/share/dasall/qemu/env.sh}
PRINT_CONFIG=0

if [ "${1:-}" = "--print-config" ]; then
  PRINT_CONFIG=1
  shift
fi

if [ -r "$QEMU_ENV_FILE" ]; then
  # shellcheck disable=SC1090
  . "$QEMU_ENV_FILE"
fi

IMAGE=${DASALL_QEMU_IMAGE:-$HOME/.cache/dasall/qemu/autopkgtest-noble-amd64.img}
KEY_FILE=${DASALL_DEEPSEEK_API_KEY_FILE:-$HOME/.local/share/dasall/secrets/deepseek-prod.secret}
SETUP_SCRIPT=${DASALL_AUTOPKGTEST_SETUP_COMMANDS:-$HOME/.local/share/dasall/qemu/setup-commands.sh}
TESTBED_SECRET_PATH=${DASALL_AUTOPKGTEST_TESTBED_SECRET_PATH:-/tmp/dasall-release/deepseek.key}
BUILD_DIR=${DASALL_BUILD_DIR:-build-ci}
TIMEOUT_REBOOT=${DASALL_AUTOPKGTEST_TIMEOUT_REBOOT:-180}
PROVIDER_PROBE_URL=${DASALL_PROVIDER_PROBE_URL:-https://api.deepseek.com}
OUTPUT_BASE=${DASALL_AUTOPKGTEST_OUTPUT_BASE:-$HOME/.cache/dasall/qemu/autopkgtest-output}
QEMU_BINARY=${DASALL_QEMU_BINARY:-/usr/bin/qemu-system-x86_64}
QEMU_NO_KVM_WRAPPER=${DASALL_QEMU_NO_KVM_WRAPPER:-$HOME/.cache/dasall/qemu/qemu-system-x86_64-no-kvm}
AUTOPKGTEST_VIRT_QEMU=${DASALL_AUTOPKGTEST_VIRT_QEMU:-/usr/bin/autopkgtest-virt-qemu}
KVM_PROBE_TIMEOUT=${DASALL_QEMU_KVM_PROBE_TIMEOUT:-2}
TIMESTAMP=$(date +%Y%m%d-%H%M%S)
OUTPUT_DIR=${DASALL_AUTOPKGTEST_OUTPUT_DIR:-${OUTPUT_BASE}/${TIMESTAMP}}
CURRENT_USER=$(id -un)
KVM_PROBE_ERROR=

probe_kvm() {
  PROBE_LOG=

  [ -w /dev/kvm ] || return 1
  command -v "$QEMU_BINARY" >/dev/null 2>&1 || return 1
  command -v timeout >/dev/null 2>&1 || return 0

  PROBE_LOG=$(mktemp "${TMPDIR:-/tmp}/dasall-kvm-probe.XXXXXX")
  if timeout "${KVM_PROBE_TIMEOUT}s" \
    "$QEMU_BINARY" -machine none -accel kvm -display none -nodefaults \
    >/dev/null 2>"$PROBE_LOG"; then
    rm -f "$PROBE_LOG"
    return 0
  fi

  PROBE_RC=$?
  if [ "$PROBE_RC" -eq 124 ]; then
    rm -f "$PROBE_LOG"
    return 0
  fi

  KVM_PROBE_ERROR=$(sed -n '1,2p' "$PROBE_LOG" | tr '\n' '; ')
  rm -f "$PROBE_LOG"
  return 1
}

ensure_no_kvm_wrapper() {
  WRAPPER_DIR=$(dirname "$QEMU_NO_KVM_WRAPPER")
  mkdir -p "$WRAPPER_DIR"
  cat >"$QEMU_NO_KVM_WRAPPER" <<EOF
#!/usr/bin/env bash
set -euo pipefail
filtered=()
for arg in "\$@"; do
  [[ "\$arg" == "-enable-kvm" ]] && continue
  filtered+=("\$arg")
done
exec "$QEMU_BINARY" "\${filtered[@]}"
EOF
  chmod +x "$QEMU_NO_KVM_WRAPPER"
}

fail() {
  printf '[run-local-qemu-gate] %s\n' "$*" >&2
  exit 1
}

if [ ! -w /dev/kvm ] && [ "${DASALL_SG_KVM:-0}" != "1" ] && command -v sg >/dev/null 2>&1; then
  if getent group kvm >/dev/null 2>&1 && getent group kvm | grep -Eq "(^|:)${CURRENT_USER}(,|$)"; then
    if [ "$PRINT_CONFIG" -eq 1 ]; then
      exec env DASALL_SG_KVM=1 sg kvm -c "sh '$SELF' --print-config"
    fi
    exec env DASALL_SG_KVM=1 sg kvm -c "sh '$SELF'"
  fi
fi

[ -d "$REPO_ROOT" ] || fail "missing repo root: $REPO_ROOT"
[ -f "$IMAGE" ] || fail "missing qemu image: $IMAGE (run setup_local_qemu_gate_env.sh first)"
[ -f "$KEY_FILE" ] || fail "missing DeepSeek key/secret file: $KEY_FILE (run setup_local_qemu_gate_env.sh first)"
[ -x "$SETUP_SCRIPT" ] || fail "missing executable guest setup script: $SETUP_SCRIPT"
mkdir -p "$OUTPUT_BASE"

export DASALL_DEEPSEEK_API_KEY_FILE="$KEY_FILE"
export DASALL_AUTOPKGTEST_SETUP_COMMANDS="$SETUP_SCRIPT"
export DASALL_AUTOPKGTEST_TESTBED_SECRET_PATH="$TESTBED_SECRET_PATH"
export DASALL_AUTOPKGTEST_OUTPUT_DIR="$OUTPUT_DIR"
export DASALL_PROVIDER_PROBE_URL="$PROVIDER_PROBE_URL"

if probe_kvm; then
  DISABLE_KVM=0
  KVM_STATE=enabled
  QEMU_COMMAND=
else
  DISABLE_KVM=1
  KVM_STATE=disabled
  QEMU_COMMAND=$QEMU_NO_KVM_WRAPPER
  ensure_no_kvm_wrapper
fi

printf '[run-local-qemu-gate] repo=%s\n' "$REPO_ROOT"
printf '[run-local-qemu-gate] image=%s\n' "$IMAGE"
printf '[run-local-qemu-gate] key=%s\n' "$KEY_FILE"
printf '[run-local-qemu-gate] setup=%s\n' "$SETUP_SCRIPT"
printf '[run-local-qemu-gate] output=%s\n' "$OUTPUT_DIR"
printf '[run-local-qemu-gate] kvm=%s\n' "$KVM_STATE"
printf '[run-local-qemu-gate] qemu-command=%s\n' "${QEMU_COMMAND:-$QEMU_BINARY}"
if [ -n "$KVM_PROBE_ERROR" ]; then
  printf '[run-local-qemu-gate] kvm-probe=%s\n' "$KVM_PROBE_ERROR"
fi

if [ "$PRINT_CONFIG" -eq 1 ]; then
  exit 0
fi

cd "$REPO_ROOT"
if [ "$DISABLE_KVM" -eq 1 ]; then
  exec sh "${SCRIPT_DIR}/validate_gate_int_10_installed_package_qemu.sh" \
    --build-dir "$BUILD_DIR" \
    --disable-kvm \
    -- "$AUTOPKGTEST_VIRT_QEMU" --timeout-reboot="$TIMEOUT_REBOOT" \
    --qemu-command="$QEMU_COMMAND" "$IMAGE"
fi

exec sh "${SCRIPT_DIR}/validate_gate_int_10_installed_package_qemu.sh" \
  --build-dir "$BUILD_DIR" \
  -- "$AUTOPKGTEST_VIRT_QEMU" --timeout-reboot="$TIMEOUT_REBOOT" "$IMAGE"
