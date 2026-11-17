#!/usr/bin/env bash
# Build the firmware locally in the ZMK Docker image, using the local
# checkout of the Prospector fork instead of the revision west.yml names
# (so unpushed fork commits can be built and tested).
#
#   scripts/build-local.sh <dongle|left|right|all> [extra cmake args...]
#
# e.g. scripts/build-local.sh dongle -DCONFIG_PROSPECTOR_ZAPPY_BIRDS=n
#
# Env: ZAPPY_WS  west workspace (default ~/.cache/zmk-zappy-ws)
#      ZAPPY_OUT build output   (default $ZAPPY_WS/out); uf2 at $ZAPPY_OUT/<target>/zephyr/zmk.uf2
#      FORK_DIR  local Prospector fork (default ../prospector-zmk-module)
set -euo pipefail

CFG_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WS="${ZAPPY_WS:-$HOME/.cache/zmk-zappy-ws}"
OUT="${ZAPPY_OUT:-$WS/out}"
FORK_DIR="$(cd "${FORK_DIR:-$CFG_ROOT/../prospector-zmk-module}" && pwd)"
IMAGE="zmkfirmware/zmk-build-arm:stable"

target="${1:?usage: $0 <dongle|left|right|all> [extra cmake args...]}"
shift
extra=("$@")

mkdir -p "$WS/config" "$OUT"
rm -rf "$WS/config" && cp -a "$CFG_ROOT/config" "$WS/config"

# Workspace mounted at /ws; the local fork shadows the west-fetched copy.
run() {
    docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -e ZEPHYR_BASE=/ws/zephyr \
        -v "$WS:/ws" -v "$FORK_DIR:/ws/prospector-zmk-module" -v "$CFG_ROOT:/zmk-config:ro" \
        -w /ws "$IMAGE" "$@"
}

if [ ! -d "$WS/.west" ]; then
    run west init -l config
    run west update
fi

build() {
    local name="$1" board="$2" shield="$3"
    echo "=== building $name ($board / $shield) ==="
    run west build -p -s zmk/app -d "/ws/out/$name" -b "$board" -- \
        -DZephyr_DIR=/ws/zephyr/share/zephyr-package/cmake -DSHIELD="$shield" -DZMK_CONFIG=/ws/config -DZMK_EXTRA_MODULES=/zmk-config \
        ${extra[@]+"${extra[@]}"}
    if [ "$OUT" != "$WS/out" ]; then
        mkdir -p "$OUT/$name/zephyr"
        cp "$WS/out/$name/zephyr/zmk.uf2" "$OUT/$name/zephyr/zmk.uf2"
    fi
    echo "=== $name: $OUT/$name/zephyr/zmk.uf2 ==="
}

case "$target" in
    dongle)
        # ZMK Studio needs this snippet (as in build.yaml); merge with any -DSNIPPET= given
        snip="studio-rpc-usb-uart"
        rest=()
        for a in ${extra[@]+"${extra[@]}"}; do
            case "$a" in -DSNIPPET=*) snip="$snip;${a#-DSNIPPET=}" ;; *) rest+=("$a") ;; esac
        done
        extra=(${rest[@]+"${rest[@]}"} "-DSNIPPET=$snip")
        build dongle seeeduino_xiao_ble "corne_dongle prospector_adapter raw_hid_adapter" ;;
    left)   build left nice_nano_v2 "corne_left nice_view_adapter nice_view" ;;
    right)  build right nice_nano_v2 "corne_right nice_view_adapter nice_view" ;;
    all)
        "$0" dongle "${extra[@]+"${extra[@]}"}"
        "$0" left "${extra[@]+"${extra[@]}"}"
        "$0" right "${extra[@]+"${extra[@]}"}"
        ;;
    *) echo "unknown target: $target" >&2; exit 2 ;;
esac
