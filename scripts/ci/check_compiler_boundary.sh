#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
TARGETS=("$ROOT/include/chronon3d/compiler" "$ROOT/src/compiler")
PATTERN='vulkan|cuda|libav|runtime/|backends/|Framebuffer|GpuRuntime'

if rg -n -i "$PATTERN" "${TARGETS[@]}" --glob '*.{hpp,cpp,inc}' ; then
    echo "compiler boundary violation" >&2
    exit 1
fi
echo "compiler boundary: PASS"
