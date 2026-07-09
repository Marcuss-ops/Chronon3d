#!/usr/bin/env bash
# tools/sdk/run_external_consumer.sh — Steps 4-5 (consumer configure/build/run
# + [BOUNDARY-OK] marker assertion + PNG non-empty verification).
#
# Builds the standalone consumer project at
# `tests/install_consumer/` against the previously-installed SDK
# prefix; runs the resulting `check_install` binary; asserts that:
#   (a) the binary produced stdout containing the [BOUNDARY-OK] marker
#   (b) the binary wrote a non-zero-size PNG to sdk_consumer_output.png
#   (c) the PNG passes a basic pixel-count probe (diagnostic only —
#       the strict-A consumer produces a near-empty framebuffer by
#       design; file-size > 0 is the authoritative gate per audit P0 #6)
#
# Companion to the Fase-3 (Step 3.5) canary gate: the canary gate
# proves the symbol is in the archive; this consumer phase proves it
# is reachable via the documented PUBLIC SURFACE (TICKET-011).
#
# Env inputs (REQUIRED):
#   SDK_PREFIX  — install prefix root (consumer CMAKE_PREFIX_PATH target)
#   REPO_ROOT   — repo root (consumer source dir is tests/install_consumer)
#
# Outputs:
#   CONS_BUILD  temp dir is created; consumer binary lives at
#               $CONS_BUILD/check_install; PNG lives at
#               $CONS_BUILD/sdk_consumer_output.png (consumer runs
#               from CONS_BUILD so PNG lands there).
#
# Invocation pattern:  bash tools/sdk/run_external_consumer.sh
#
# Exit codes:
#   0 = consumer rendered correctly + [BOUNDARY-OK] + PNG non-empty
#   1 = any failure (configure / build / missing binary / no marker /
#                    missing or empty / pixel probe failure)

HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=./common.sh
source "$HERE/common.sh"

# ── Required env vars ─────────────────────────────────────────────────
: "${SDK_PREFIX:?SDK_PREFIX env var required}"
: "${REPO_ROOT:?REPO_ROOT env var required (consumer source is tests/install_consumer)}"

# ── Consumer temp build dir (orchestrator-managed on entry; cleanup here) ──
CONS_BUILD="$(mktemp_dir chronon3d_install_consumer_build)"
cleanup_register "$CONS_BUILD"

log "consumer build starting (CONS_BUILD=$CONS_BUILD)"

# ── Phase 4 STRICT pre-build source discipline (P3-H gate) ─────────────
# These five static greps enforce the P3-H RELEASE_GATE.md §SDK Product V1/8
# invariants without waiting for the compile+run+PNG cycle.  Any miss ⇒
# fail() with a precise diagnostic.
CONSUMER_SRC="$REPO_ROOT/tests/install_consumer/main.cpp"
log "P3-H strict pre-build: static greps on $CONSUMER_SRC"
[[ -f "$CONSUMER_SRC" ]] || fail "consumer source missing: $CONSUMER_SRC"

# (2) Manifest-clean: NO advanced/, internal.hpp, runtime.hpp, test/*.
BANNED_RX='#include[[:space:]]*<(advanced/|.*/advanced/|.*internal\.hpp|.*runtime\.hpp|.*/test/[a-z])>'
banned_hits="$(grep -nE "$BANNED_RX" "$CONSUMER_SRC" 2>/dev/null || true)"
if [[ -n "$banned_hits" ]]; then
    log "Banned-path includes found:"
    printf '%s\n' "$banned_hits" >&2
    fail "phase4 strict: consumer source contains banned-path includes"
fi

# (3a) Productive text run present.
text_hits="$(grep -cE 'TextShape|LayerKind::Text' "$CONSUMER_SRC" 2>/dev/null || echo 0)"
text_hits="${text_hits//[[:space:]]/}"   # strip whitespace
if [[ "${text_hits:-0}" -lt 1 ]]; then
    fail "phase4 strict: consumer source missing TextShape / LayerKind::Text (productive text run required)"
fi
log "  - text layer markers: $text_hits hit(s)"

# (3b) Compiled camera present (CameraDescriptor + setter on Composition).
camera_hits="$(grep -cE 'camera_v1::CameraDescriptor|default_camera_descriptor' "$CONSUMER_SRC" 2>/dev/null || echo 0)"
camera_hits="${camera_hits//[[:space:]]/}"
if [[ "${camera_hits:-0}" -lt 1 ]]; then
    fail "phase4 strict: consumer source missing camera_v1::CameraDescriptor / default_camera_descriptor"
fi
log "  - camera markers: $camera_hits hit(s)"

# (4a) sdk::RenderEngine used ≥ 1 (P3-H productive engine path).
sdk_hits="$(grep -cE '(^|[^A-Za-z0-9_])(chronon3d|c3d)::sdk::RenderEngine' "$CONSUMER_SRC" 2>/dev/null || echo 0)"
sdk_hits="${sdk_hits//[[:space:]]/}"
if [[ "${sdk_hits:-0}" -lt 1 ]]; then
    fail "phase4 strict: consumer source does NOT use chronon3d::sdk::RenderEngine"
fi
log "  - sdk::RenderEngine markers: $sdk_hits hit(s)"

# (4b) Legacy bare-namespace RenderEngine class NOT used.
# Three USAGE-shaped patterns are matched (NOT mere doc references):
#   (a) variable declaration:    c3d::RenderEngine engine;       / with brace-init
#   (b) pointer declaration:     c3d::RenderEngine* p;
#   (c) static method call:      c3d::RenderEngine::render(...)
# After `RenderEngine`, the regex requires either `\s+[a-zA-Z_]` (var/brace
# decl), `\s*\*` (pointer decl), or `::` (member/static acccess).  A
# bare textual reference in a comment (e.g. "the legacy
# chronon3d::RenderEngine") followed by end-of-line does NOT match.
# `sdk::` on the same line is then excluded explicitly.
legacy_rx='(chronon3d|c3d)::RenderEngine(\s+[a-zA-Z_]|\s*\*|::)'
legacy_hits="$(grep -nE "$legacy_rx" "$CONSUMER_SRC" 2>/dev/null \
    | grep -v 'sdk::' || true)"
if [[ -n "$legacy_hits" ]]; then
    log "Legacy RenderEngine (non-sdk) usage found:"
    printf '%s\n' "$legacy_hits" >&2
    fail "phase4 strict: consumer source still uses legacy chronon3d::RenderEngine (deprecated)"
fi
log "P3-H strict pre-build: all five static checks passed"

# ── Configure consumer respecting vcpkg / existing toolchain ─────────
CONS_PREFIX_PATH="$SDK_PREFIX"
if [[ -n "${VCPKG_INSTALLED_DIR:-}" ]]; then
    local_vcpkg_triplet="${VCPKG_TARGET_TRIPLET:-x64-linux}"
    CONS_PREFIX_PATH="${CONS_PREFIX_PATH};${VCPKG_INSTALLED_DIR}/${local_vcpkg_triplet}"
fi
if [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
    CONS_PREFIX_PATH="${CONS_PREFIX_PATH};${CMAKE_PREFIX_PATH}"
fi

CMAKE_ARGS=(
    "-S" "$REPO_ROOT/tests/install_consumer"
    "-B" "$CONS_BUILD"
    "-DCMAKE_PREFIX_PATH=$CONS_PREFIX_PATH"
    "-DCMAKE_BUILD_TYPE=Release"
    "-DVCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR:-}"
    "-DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET:-x64-linux}"
)
VCPKG_TC="$REPO_ROOT/vcpkg_bootstrap/scripts/buildsystems/vcpkg.cmake"
if [[ -f "$VCPKG_TC" ]]; then
    CMAKE_ARGS+=( "-DCMAKE_TOOLCHAIN_FILE=$VCPKG_TC" )
elif [[ -n "${CMAKE_TOOLCHAIN_FILE:-}" ]]; then
    CMAKE_ARGS+=( "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}" )
fi
cmake "${CMAKE_ARGS[@]}" 1>&2 \
    || fail "consumer cmake configure failed"

# ── Build consumer ────────────────────────────────────────────────────
cmake --build "$CONS_BUILD" --target check_install 1>&2 \
    || fail "consumer cmake --build failed"

consumer_bin="$CONS_BUILD/check_install"
[[ -x "$consumer_bin" ]] || fail "consumer binary not found at $consumer_bin"

# ── Helpers (kept local — no need to pollute common.sh) ───────────────
# Fix (audit P0 #5): helper moved BEFORE first use (was after exit 0).
# check_pixel_hash_strict — P3-H strict check: ≥ 1 PNG pixel must have
# mean RGB luminance > 5/255 (thus R+G+B > 15 in 8-bit).  Replaces the
# previous diagnostic-only count_non_zero_pixels() with a HARD fail
# when the rendered PNG would be black (audit P0 #6 follow-up).
# Return codes:
#   0 = PASS (≥ 1 such pixel found); stdout holds a diagnostic line.
#   1 = FAIL (zero such pixels → output PNG is black/below threshold).
#   2 = TOOL-MISSING (neither python3+PIL nor ImageMagick available).
check_pixel_hash_strict() {
    local png="$1"

    # Python + PIL primary path.  convert('RGBA') guarantees 4-channel
    # data; we sum per-channel byte values; threshold R+G+B > 15 picks
    # up any pixel with mean RGB > 5/255.
    if command -v python3 >/dev/null 2>&1; then
        local py_rc py_out
        set +e
        py_out="$(python3 - "$png" <<'PYEOF' 2>/dev/null)
import sys
from PIL import Image
img = Image.open(sys.argv[1]).convert('RGBA')
w, h = img.size
above = 0
for r, g, b, a in img.getdata():
    if a > 5 or r > 5 or g > 5 or b > 5:
        above += 1
img.close()
print(f'PASS above={above}/{w*h} (any_channel>5/255 alpha-aware)')
sys.exit(0 if above >= 1 else 1)
PYEOF
)"
        py_rc=$?
        set -e
        if [[ "$py_rc" -eq 0 ]]; then
            printf '%s' "$py_out"
            return 0
        fi
        if [[ "$py_rc" -eq 1 ]]; then
            return 1
        fi
        # py_rc=2 ⇒ PIL not installed (ImportError).  Fall through to IM.
    fi

    # ImageMagick fallback — alpha-aware per-channel max.
    # Uses %[max] on RGB and Alpha channels separately (not %[fx:mean]
    # which dilutes RGB with alpha and misses alpha-only content).
    # Threshold: any channel max > 5/255 ≈ 0.0196 (matches Python+PIL path).
    if command -v identify >/dev/null 2>&1; then
        local rgb_max alpha_max
        rgb_max="$(identify -channel RGB -format '%[max]' "$png" 2>/dev/null || true)"
        alpha_max="$(identify -channel A -format '%[max]' "$png" 2>/dev/null || true)"
        if [[ -z "$rgb_max" && -z "$alpha_max" ]]; then
            return 2   # tool failed — treat as missing
        fi
        if awk -v r="${rgb_max:-0}" -v a="${alpha_max:-0}" \
               'BEGIN { exit !(r > 0.0196 || a > 0.0196) }'; then
            printf 'PASS (ImageMagick alpha-aware max-RGB=%s max-A=%s > 0.0196)' \
                   "${rgb_max:-0}" "${alpha_max:-0}"
            return 0
        else
            return 1
        fi
    fi

    return 2   # neither Python+PIL nor ImageMagick available
}

# ── Run consumer + assert [BOUNDARY-OK] ───────────────────────────────
# Fix (audit P0 #5): run consumer from CONS_BUILD so the PNG written
# to CWD (sdk_consumer_output.png — see tests/install_consumer/main.cpp)
# lands inside CONS_BUILD alongside the build artifacts.
log "running consumer: $consumer_bin (CWD=$CONS_BUILD)"
consumer_output="$(cd "$CONS_BUILD" && "$consumer_bin")"
if [[ "$consumer_output" != *"[BOUNDARY-OK]"* ]]; then
    log "Consumer stdout:"
    printf "%s\n" "$consumer_output" >&2
    fail "consumer missing [BOUNDARY-OK] marker in stdout"
fi
log "Consumer: $consumer_output"

# ── Verify PNG non-empty (cheap file size check) ──────────────────────
# Fix (audit P0 #5): PNG now lives in CONS_BUILD because the consumer
# runs with CWD=$CONS_BUILD.
output_png="$CONS_BUILD/sdk_consumer_output.png"
[[ -f "$output_png" ]] || fail "output PNG not found: $output_png"
png_size="$(stat -c%s "$output_png" 2>/dev/null || echo 0)"
(( png_size > 0 )) || fail "output PNG is empty: $output_png"

# ── STRICT PIXEL-HASH VERIFICATION (≥ 1 pixel with mean RGB > 5/255) ──
# Replaces the previous "DIAGNOSTIC only" probe: BOUNDARY-OK + exit 0
# ONLY if ≥ 1 pixel has mean RGB luminance > 5/255.  File size > 0
# alone is no longer authoritative — a black-framebuffer PNG of size N
# would have passed the old gate.  Helper rc:
#   0 = PASS;  1 = FAIL (PNG near-black);  2 = tool-missing.
set +e
pixel_hash_out="$(check_pixel_hash_strict "$output_png")"
hash_rc=$?
set -e
case "$hash_rc" in
    0) log "PNG strict pixel-hash verified: $output_png ($png_size bytes, $pixel_hash_out)" ;;
    1) fail "phase4 strict: PNG is near-black (0 pixels with mean RGB > 5/255). Output: $output_png" ;;
    2) fail "phase4 strict: cannot verify pixel-hash — install python3+PIL or ImageMagick" ;;
    *) fail "phase4 strict: check_pixel_hash_strict returned unknown rc=$hash_rc" ;;
esac

exit 0
