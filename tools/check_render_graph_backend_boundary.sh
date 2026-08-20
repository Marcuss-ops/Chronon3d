#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fail=0

if rg -n 'backends/software/include_private' "$root/src/render_graph"; then
    echo "GATE_FAIL: render_graph receives a software private include directory" >&2
    fail=1
fi

if rg -n '^#include .*backends/software/' \
    "$root/include/chronon3d/render_graph" --glob '*.hpp'; then
    echo "GATE_FAIL: public render_graph header includes a concrete software implementation" >&2
    fail=1
fi

if rg -n 'utils/video/(native_video_frame_decoder|cuda_nv12_surface_compositor)' \
    "$root/apps/chronon3d_cli" --glob '*.{cpp,h,hpp}' 2>/dev/null; then
    echo "GATE_FAIL: CLI still owns media decoder or CUDA/Vulkan compositor implementation" >&2
    fail=1
fi

if ((fail)); then exit 1; fi
echo "GATE_PASS: render_graph headers and CMake do not depend on software private headers"
