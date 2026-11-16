#!/usr/bin/env bash
# Draw config/corne.keymap as docs/keymap.svg with keymap-drawer
# (https://github.com/caksoylar/keymap-drawer). Needs uv. Also run by
# .github/workflows/draw-keymap.yml on every keymap change.
set -euo pipefail
cd "$(dirname "$0")/.."
KD="${KEYMAP_DRAWER:-keymap-drawer==0.23.0}"   # pip spec
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
run() { uv run -q --no-project --with "$KD" "$@"; }

run keymap --version
run keymap -c keymap_drawer.config.yaml parse -z config/corne.keymap -o "$tmp/parsed.yaml"
run python scripts/keymap_36.py "$tmp/parsed.yaml" "$tmp/corne.yaml"
run keymap -c keymap_drawer.config.yaml draw "$tmp/corne.yaml" -o docs/keymap.svg
echo "wrote docs/keymap.svg"
