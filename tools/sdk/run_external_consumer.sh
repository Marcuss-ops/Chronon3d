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
# Fix (audit P0 #5): moved BEFORE first use (was after exit 0).
count_non_zero_pixels() {
    local png="$1"
    local py
    py="$(printf 'python3 -c "from PIL import Image; img=Image.open(%q); print(sum(1 for p in img.getdata() if p != (0,0,0,0)))"' "$png")"
    local out
    if out="$(bash -c "$py" 2>/dev/null)"; then
        printf '%s' "$out"
        return 0
    fi
    # ImageMagick fallback (heuristic): mean * w * h is a coarse proxy
    # for total intensity; acceptable as a best-effort sanity check.
    out="$(identify -format '%[fx:int(mean*w*h)]' "$png" 2>/dev/null || echo '')"
    printf '%s' "$out"
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

# ── Non-zero pixel-count probe (Python+PIL preferred; IM fallback) ────
# Fix (audit P0 #6): the strict-A consumer explicitly declares a near-
# empty / all-black framebuffer is the expected outcome (empty SceneFn
# → no layers → blank output).  The 1000-pixel minimum has been
# REMOVED; the pixel probe is now DIAGNOSTIC only.  File size > 0
# (checked above) is the authoritative gate for PNG round-trip success.
non_zero="$(count_non_zero_pixels "$output_png" 2>/dev/null || echo '')"
if [[ -n "$non_zero" ]]; then
    log "PNG verified: $output_png ($png_size bytes, $non_zero non-zero pixels)"
else
    log "PNG verified: $output_png ($png_size bytes, pixel probe skipped — Python+PIL / ImageMagick not available)"
fi

exit 0
