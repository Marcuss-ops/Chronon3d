#!/usr/bin/env bash
# check_no_runtime_image_decode.sh
#
# Rendering processors may only consume prepared image data.  File I/O and
# image decoding belong to render preparation or the image-cache loader.

set -euo pipefail

GATE_NAME=check_no_runtime_image_decode
REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$REPO_ROOT"

SCAN_PATHS=(src/render_graph src/backends/software)
PATTERN='load_image|decode_image|decode_and_store|stbi_load|BLImage::readFromFile'

matches="$(rg -n --hidden \
    --glob '*.{h,hpp,hh,c,cc,cpp,cxx}' \
    -e "$PATTERN" "${SCAN_PATHS[@]}" 2>/dev/null \
    | rg -v 'render_preparation|resource_preparation|image_cache|image_loader|test' \
    || true)"

if [[ -n "$matches" ]]; then
    echo "GATE_FAIL: runtime image I/O/decode found outside preparation/cache:" >&2
    echo "$matches" | sed 's/^/  /' >&2
    exit 1
fi

echo "GATE_PASS: no image I/O or decode in render processors"
echo "[INFO] ${GATE_NAME}: frame rendering consumes prepared image data only"
exit 0
