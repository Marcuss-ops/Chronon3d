#!/usr/bin/env bash
# tools/sdk/check_cabi_self_contained.sh
#
# Verifies the self-contained binary-package contract of the C ABI shared
# library (libchronon3d_c.so).  Go / Rust / Python / cgo consumers must be
# able to link and run against the .so WITHOUT installing Chronon's build-time
# dependencies (glm, harfbuzz, freetype, blend2d, xxhash, TBB, ...).
#
# Contract: the .so's dynamic NEEDED entries must contain ONLY the standard
# system runtime (libc / libm / libstdc++ / libgcc_s / ld-linux); every
# third-party dependency must already be statically linked inside the .so.
# A regression that turns one of those libs back into a runtime dependency
# makes this gate fail.
#
# Exit codes:
#   0 = self-contained (no third-party NEEDED entries)
#   1 = gate failed (missing .so / readelf failed / third-party NEEDED found)
#
# Env inputs:
#   SDK_PREFIX  — install prefix root (libchronon3d_c.so lookup)
#   CABI_SO     — optional override: direct path to libchronon3d_c.so
#
# Invocation pattern:  bash tools/sdk/check_cabi_self_contained.sh

HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=./common.sh
source "$HERE/common.sh"

so="${CABI_SO:-}"
if [[ -z "$so" ]]; then
    : "${SDK_PREFIX:?SDK_PREFIX or CABI_SO env var required}"
    so="$(find "$SDK_PREFIX" -name 'libchronon3d_c.so' 2>/dev/null | head -1 || true)"
    [[ -z "$so" ]] && so="$(find "$SDK_PREFIX" -name 'libchronon3d_c.so.*' -type f 2>/dev/null | head -1 || true)"
fi
[[ -n "$so" && -e "$so" ]] || fail "libchronon3d_c.so not found"

log "self-contained gate starting (so=$so)"

command -v readelf >/dev/null 2>&1 || fail "readelf not on PATH"

needed="$(readelf -d "$so" 2>/dev/null | awk '/\(NEEDED\)/ {gsub(/\[|\]/,"",$NF); print $NF}')"
if [[ -z "$needed" ]]; then
    fail "readelf -d produced no NEEDED entries for $so (unreadable or not an ELF shared object)"
fi

log "NEEDED entries:"
printf '%s\n' "$needed" | sed 's/^/    /' >&2

# Third-party libraries that MUST be statically linked into the .so, never
# required at consumer runtime.  Keep this list in sync with the SDK's
# CHRONON3D_SDK_PUBLIC_DEPS and the optional feature deps (freetype/harfbuzz/
# blend2d/ffmpeg/meshoptimizer/OpenEXR).
third_party_rx='harfbuzz|freetype|blend2d|xxhash|tbb|spdlog|fmt|nlohmann|glm|glfw|vulkan|openexr|meshoptimizer|fribidi|avcodec|avformat|avutil|swscale|swresample|libpng|libjpeg|zlib|bz2|lzma'

hits="$(printf '%s\n' "$needed" | grep -iE "$third_party_rx" || true)"
if [[ -n "$hits" ]]; then
    log "third-party runtime dependencies leaked into the C ABI .so:"
    printf '%s\n' "$hits" | sed 's/^/    /' >&2
    fail "libchronon3d_c.so is not self-contained"
fi

count="$(printf '%s\n' "$needed" | wc -l | tr -d ' ')"
log "self-contained gate PASS ($count system NEEDED entries, 0 third-party)"
exit 0
