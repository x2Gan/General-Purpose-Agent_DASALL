#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "${SCRIPT_DIR}/../.." && pwd)

CACHE_QEMU_DIR=${DASALL_LOCAL_QEMU_CACHE_DIR:-$HOME/.cache/dasall/qemu}
LOCAL_SHARE_QEMU_DIR=${DASALL_LOCAL_QEMU_SHARE_DIR:-$HOME/.local/share/dasall/qemu}
LOCAL_SECRET_DIR=${DASALL_LOCAL_QEMU_SECRET_DIR:-$HOME/.local/share/dasall/secrets}

IMAGE_DST=${DASALL_LOCAL_QEMU_IMAGE_PATH:-${CACHE_QEMU_DIR}/autopkgtest-noble-amd64.img}
KEY_DST=${DASALL_LOCAL_DEEPSEEK_KEY_PATH:-${LOCAL_SECRET_DIR}/deepseek-prod.secret}
BUILD_DIR=${DASALL_LOCAL_QEMU_BUILD_DIR:-build-ci}
TIMEOUT_REBOOT=${DASALL_LOCAL_QEMU_TIMEOUT_REBOOT:-180}
PROVIDER_PROBE_URL=${DASALL_LOCAL_PROVIDER_PROBE_URL:-https://api.deepseek.com}
TESTBED_SECRET_PATH=${DASALL_LOCAL_TESTBED_SECRET_PATH:-/tmp/dasall-release/deepseek.key}

IMAGE_SOURCE=
KEY_SOURCE=/var/lib/dasall/secrets/llm/providers/deepseek-prod.secret
FORCE_COPY=0

log() {
  printf '[setup-local-qemu-gate-env] %s\n' "$*"
}

fail() {
  printf '[setup-local-qemu-gate-env] %s\n' "$*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Usage: sh scripts/packaging/setup_local_qemu_gate_env.sh [options]

Options:
  --image PATH              Existing qemu/autopkgtest image to copy into the
                            stable local cache path.
  --deepseek-key-file PATH  Existing host-side DeepSeek key/secret file to copy
                            into the stable local secret path.
  --build-dir DIR           Build directory used by the local qemu gate wrapper.
                            Defaults to build-ci.
  --timeout-reboot SEC      autopkgtest-virt-qemu reboot timeout. Defaults to 180.
  --provider-probe-url URL  Provider reachability probe URL. Defaults to
                            https://api.deepseek.com.
  --testbed-secret-path P   Guest-side path used by autopkgtest --copy.
                            Defaults to /tmp/dasall-release/deepseek.key.
  --force                   Overwrite the stable image/key even if they already
                            exist.
  -h, --help                Show this help text.

Behavior:
  - Creates stable local cache/share directories under $HOME.
  - Copies the qemu image and DeepSeek secret into stable local paths.
  - Writes an env file and guest-side setup script consumed by
    scripts/packaging/run_local_qemu_gate.sh.

Notes:
  - If --image is omitted, the script reuses the existing stable image at
    $HOME/.cache/dasall/qemu/autopkgtest-noble-amd64.img.
  - If --deepseek-key-file is omitted, the script defaults to
    /var/lib/dasall/secrets/llm/providers/deepseek-prod.secret.
EOF
}

copy_into_user_path() {
  src=$1
  dst=$2

  if [ -r "$src" ]; then
    install -m 600 "$src" "$dst"
    return 0
  fi

  command -v sudo >/dev/null 2>&1 || fail "source file is not readable without sudo: $src"
  sudo -n install -m 600 "$src" "$dst" || fail "failed to copy $src with sudo"
  sudo -n chown "$(id -un):$(id -gn)" "$dst" || fail "failed to chown $dst back to current user"
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --image)
      [ "$#" -ge 2 ] || fail 'missing value for --image'
      IMAGE_SOURCE=$2
      shift 2
      ;;
    --deepseek-key-file)
      [ "$#" -ge 2 ] || fail 'missing value for --deepseek-key-file'
      KEY_SOURCE=$2
      shift 2
      ;;
    --build-dir)
      [ "$#" -ge 2 ] || fail 'missing value for --build-dir'
      BUILD_DIR=$2
      shift 2
      ;;
    --timeout-reboot)
      [ "$#" -ge 2 ] || fail 'missing value for --timeout-reboot'
      TIMEOUT_REBOOT=$2
      shift 2
      ;;
    --provider-probe-url)
      [ "$#" -ge 2 ] || fail 'missing value for --provider-probe-url'
      PROVIDER_PROBE_URL=$2
      shift 2
      ;;
    --testbed-secret-path)
      [ "$#" -ge 2 ] || fail 'missing value for --testbed-secret-path'
      TESTBED_SECRET_PATH=$2
      shift 2
      ;;
    --force)
      FORCE_COPY=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      usage
      fail "unknown option: $1"
      ;;
  esac
done

mkdir -p "$CACHE_QEMU_DIR" "$LOCAL_SHARE_QEMU_DIR" "$LOCAL_SECRET_DIR"

if [ -n "$IMAGE_SOURCE" ]; then
  [ -f "$IMAGE_SOURCE" ] || fail "missing qemu image: $IMAGE_SOURCE"
  if [ "$IMAGE_SOURCE" = "$IMAGE_DST" ]; then
    log "qemu image source already matches stable cache path: $IMAGE_DST"
  elif [ "$FORCE_COPY" -eq 1 ] || [ ! -f "$IMAGE_DST" ]; then
    log "copying qemu image into stable cache: $IMAGE_DST"
    install -m 600 "$IMAGE_SOURCE" "$IMAGE_DST"
  else
    log "stable qemu image already exists, keeping current file: $IMAGE_DST"
  fi
elif [ -f "$IMAGE_DST" ]; then
  log "reusing existing stable qemu image: $IMAGE_DST"
else
  fail "missing stable qemu image; rerun with --image <path> or create one with autopkgtest-build-qemu"
fi

[ -f "$KEY_SOURCE" ] || fail "missing DeepSeek key/secret source file: $KEY_SOURCE"
if [ "$KEY_SOURCE" = "$KEY_DST" ]; then
  log "DeepSeek key/secret source already matches stable local path: $KEY_DST"
elif [ "$FORCE_COPY" -eq 1 ] || [ ! -f "$KEY_DST" ]; then
  log "copying DeepSeek key/secret into stable local path: $KEY_DST"
  copy_into_user_path "$KEY_SOURCE" "$KEY_DST"
else
  log "stable DeepSeek key/secret already exists, keeping current file: $KEY_DST"
fi

ENV_FILE="$LOCAL_SHARE_QEMU_DIR/env.sh"
SETUP_SCRIPT="$LOCAL_SHARE_QEMU_DIR/setup-commands.sh"

cat > "$ENV_FILE" <<EOF
export DASALL_QEMU_IMAGE="$IMAGE_DST"
export DASALL_DEEPSEEK_API_KEY_FILE="$KEY_DST"
export DASALL_AUTOPKGTEST_SETUP_COMMANDS="$SETUP_SCRIPT"
export DASALL_AUTOPKGTEST_TESTBED_SECRET_PATH="$TESTBED_SECRET_PATH"
export DASALL_BUILD_DIR="$BUILD_DIR"
export DASALL_AUTOPKGTEST_TIMEOUT_REBOOT="$TIMEOUT_REBOOT"
export DASALL_PROVIDER_PROBE_URL="$PROVIDER_PROBE_URL"
EOF
chmod 600 "$ENV_FILE"

cat > "$SETUP_SCRIPT" <<EOF
#!/bin/sh
set -eu

secret_src=\${DASALL_DEEPSEEK_API_KEY_FILE:-$TESTBED_SECRET_PATH}
secret_dst=/var/lib/dasall/secrets/llm/providers/deepseek-prod.secret

if [ -f "\$secret_src" ]; then
  install -d -m 750 /var/lib/dasall /var/lib/dasall/secrets /var/lib/dasall/secrets/llm /var/lib/dasall/secrets/llm/providers
  install -m 600 "\$secret_src" "\$secret_dst"
  if getent group dasall >/dev/null 2>&1; then
    chgrp dasall /var/lib/dasall/secrets /var/lib/dasall/secrets/llm /var/lib/dasall/secrets/llm/providers "\$secret_dst" || true
    chmod 750 /var/lib/dasall/secrets /var/lib/dasall/secrets/llm /var/lib/dasall/secrets/llm/providers || true
    chmod 640 "\$secret_dst" || true
  fi
fi

if [ -n "\${DASALL_PROVIDER_PROBE_URL:-}" ] && command -v curl >/dev/null 2>&1; then
  curl -fsSIL --connect-timeout 15 "\$DASALL_PROVIDER_PROBE_URL" >/dev/null
fi
EOF
chmod 700 "$SETUP_SCRIPT"

log "repo root: $REPO_ROOT"
log "stable qemu image: $IMAGE_DST"
log "stable DeepSeek key/secret: $KEY_DST"
log "generated env file: $ENV_FILE"
log "generated guest setup script: $SETUP_SCRIPT"
log "next step: sh scripts/packaging/run_local_qemu_gate.sh --print-config"
