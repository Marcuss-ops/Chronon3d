#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
decoder="$root/src/media/video/native_video_frame_decoder.cpp"

if [[ ! -f "$decoder" ]]; then
    echo "GATE_FAIL: native decoder source is missing" >&2
    exit 1
fi

# A Session owns one reusable native CUDA/Vulkan surface. Such a surface is
# mutable, so its Framebuffer wrapper must never enter the frame cache. The
# CPU path remains cacheable; this guard prevents an A→B→A alias regression
# until native surfaces become immutable ring entries.
if ! rg -q 'const bool native_surface = result->surface_handle\(\)' "$decoder" ||
   ! rg -q 'if \(!native_surface\) \{' "$decoder" ||
   ! rg -q 'session->cache\[target\] = result;' "$decoder" ||
   ! rg -q 'native_backend->release_surface\(native_surface\)' "$decoder" ||
   ! rg -q 'native_surface_registry->release\(native_surface\)' "$decoder" ||
   ! rg -q 'is_native_surface_valid\(session->native_surface\)' "$decoder"; then
    echo "GATE_FAIL: native decoder cache does not visibly exclude mutable native surfaces" >&2
    exit 1
fi

echo "GATE_PASS: native decoder cache excludes mutable GPU surface wrappers"
