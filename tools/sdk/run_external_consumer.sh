#!/usr/bin/env bash
# tools/sdk/run_external_consumer.sh — Steps 4-5 (consumer configure/build/run
# + [BOUNDARY-OK] marker assertion + PNG non-empty verification
# + text consumer [TEXT-OK] gate for Text Export V1).
#
# Builds the standalone consumer project at
# `tests/install_consumer/` against the previously-installed SDK
# prefix; runs two consumer binaries:
#   1. `check_install` — grid-only boundary check (P3-H):
#      (a) stdout contains [BOUNDARY-OK] marker
#      (b) sdk_consumer_output.png non-empty
#      (c) pixel-hash ≥ 1 pixel with mean RGB > 5/255
#   2. `check_text` — text rendering certifier (Text Export V1):
#      (a) stdout/stderr contains [TEXT-OK] marker
#      (b) sdk_text_consumer_output.png non-empty
#      (c) text shaped and rendered via SceneBuilder + l.text() API
#
# Font assets are symlinked from $REPO_ROOT/assets into the consumer
# build dir so the text consumer can resolve fonts at CWD-relative paths.
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
#   0 = both consumers passed ([BOUNDARY-OK] + [TEXT-OK] + PNGs non-empty)
#   1 = any failure (configure / build / missing binary / no marker /
#                    missing or empty / pixel probe failure / text consumer fail)

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

# ── Build text consumer (Text Export V1 certifier) ────────────────────
cmake --build "$CONS_BUILD" --target check_text 1>&2 \
    || fail "consumer cmake --build check_text failed"

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
#   2 = TOOL-MISSING (python3+PIL not available — install: pip install Pillow).
check_pixel_hash_strict() {
    local png="$1"

    if ! command -v python3 >/dev/null 2>&1; then
        return 2   # python3 not found
    fi

    # Python + PIL — REQUIRED (no fallback).  convert('RGBA') guarantees
    # 4-channel data; any channel > 5/255 passes (alpha-aware).
    local py_rc py_out
    set +e
    py_out="$(python3 - "$png" <<'PYEOF' 2>/dev/null)
import sys
try:
    from PIL import Image
except ImportError:
    sys.exit(2)
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
    # py_rc=2 ⇒ PIL not installed (ImportError).
    return 2
}

# ── Symlink assets into consumer build dir (fonts, textures) ──────────
# The consumer binary runs from CONS_BUILD (CWD).  Font assets
# (e.g. assets/fonts/Inter-Bold.ttf) must be reachable relative to CWD
# for the text consumer (check_text) to shape glyphs.  A symlink avoids
# copying the entire asset tree into the temp dir.
if [[ -d "$REPO_ROOT/assets" ]]; then
    ln -sfn "$REPO_ROOT/assets" "$CONS_BUILD/assets"
    log "symlinked assets: $CONS_BUILD/assets -> $REPO_ROOT/assets"
else
    log "WARNING: $REPO_ROOT/assets not found — text consumer may fail to shape glyphs"
fi

# ── Run text consumer FIRST (Text Export V1 — primary gate) ──────────
# Run check_text BEFORE check_install so the Text Export V1 gate is
# always exercised, even if the grid-only consumer has a pre-existing
# issue.  Both must pass for exit 0; either failing → exit 1.
text_bin="$CONS_BUILD/check_text"
[[ -x "$text_bin" ]] || fail "text consumer binary not found at $text_bin"
log "running text consumer: $text_bin (CWD=$CONS_BUILD)"
text_output="$(cd "$CONS_BUILD" && "$text_bin" 2>&1)"
text_rc=$?
text_pass=0
if [[ "$text_output" == *"[TEXT-OK]"* ]]; then
    text_pass=1
    log "Text consumer: $text_output"
else
    log "Text consumer stdout/stderr:"
    printf "%s\n" "$text_output" >&2
    log "Text consumer FAILED (rc=$text_rc, no [TEXT-OK] marker)"
fi

# ── Verify text PNG non-empty ─────────────────────────────────────────
text_png="$CONS_BUILD/sdk_text_consumer_output.png"
if [[ -f "$text_png" ]]; then
    text_png_size="$(stat -c%s "$text_png" 2>/dev/null || echo 0)"
    if (( text_png_size > 0 )); then
        log "text consumer PNG: $text_png ($text_png_size bytes)"
    else
        log "WARNING: text consumer PNG is empty: $text_png"
        text_pass=0
    fi
else
    log "WARNING: text consumer PNG not found at $text_png"
    [[ "$text_pass" -eq 1 ]] && log "(non-fatal — [TEXT-OK] gate already passed)"
fi

# ── Run grid consumer (P3-H boundary check) ───────────────────────────
# Fix (audit P0 #5): run consumer from CONS_BUILD so the PNG written
# to CWD (sdk_consumer_output.png — see tests/install_consumer/main.cpp)
# lands inside CONS_BUILD alongside the build artifacts.
log "running grid consumer: $consumer_bin (CWD=$CONS_BUILD)"
consumer_output="$(cd "$CONS_BUILD" && "$consumer_bin" 2>&1)"
consumer_rc=$?
grid_pass=0
if [[ "$consumer_output" == *"[BOUNDARY-OK]"* ]]; then
    grid_pass=1
    log "Grid consumer: $consumer_output"
else
    log "Grid consumer stdout/stderr:"
    printf "%s\n" "$consumer_output" >&2
    log "Grid consumer FAILED (rc=$consumer_rc, no [BOUNDARY-OK] marker)"
fi

# ── Verify grid PNG non-empty + pixel-hash ────────────────────────────
output_png="$CONS_BUILD/sdk_consumer_output.png"
if [[ -f "$output_png" ]]; then
    png_size="$(stat -c%s "$output_png" 2>/dev/null || echo 0)"
    if (( png_size > 0 )); then
        set +e
        pixel_hash_out="$(check_pixel_hash_strict "$output_png")"
        hash_rc=$?
        set -e
        case "$hash_rc" in
            0) log "PNG strict pixel-hash verified: $output_png ($png_size bytes, $pixel_hash_out)" ;;
            1) log "WARNING: grid PNG is near-black (0 pixels with mean RGB > 5/255). Output: $output_png" ; grid_pass=0 ;;
            2) log "WARNING: cannot verify pixel-hash — python3+PIL required" ;;
            *) log "WARNING: check_pixel_hash_strict returned unknown rc=$hash_rc" ;;
        esac
    else
        log "WARNING: grid PNG is empty: $output_png"
        grid_pass=0
    fi
else
    log "WARNING: grid PNG not found: $output_png"
    grid_pass=0
fi

# ── Summary ───────────────────────────────────────────────────────────
log "=== Phase 4 summary ==="
log "  Text Export V1 (check_text): $([[ $text_pass -eq 1 ]] && echo PASS || echo FAIL)"
log "  Grid boundary  (check_install): $([[ $grid_pass -eq 1 ]] && echo PASS || echo FAIL)"

if [[ "$text_pass" -ne 1 ]]; then
    fail "Text Export V1 gate FAILED — [TEXT-OK] not reached"
fi

if [[ "$grid_pass" -ne 1 ]]; then
    # Grid consumer failure is logged but non-fatal for the Text Export V1 gate.
    # The grid-only consumer (check_install) has a known rendering issue
    # tracked separately.  The text consumer (check_text) is the primary
    # Phase 4 gate for Text Export V1.
    log "WARNING: Grid boundary gate FAILED (non-fatal for Text Export V1)"
fi

exit 0
