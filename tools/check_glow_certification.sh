#!/usr/bin/env bash
# check_glow_certification.sh — Glow certification gate.
#
# TICKET-GLOW-CERTIFICATION — Azione 4: canonical glow acceptance gate.
#
# Wired tests (from Azioni 1-3):
#   - GlowAcceptance   (6 TEST_CASEs: intensity-zero, radius, additive,
#                        anti-clip, no-rect-edge, state-leak)
#   - GlowTemporal     (3 TEST_CASEs: 60-frame sweep, pulse timing,
#                        MP4 SSIM forward-point)
#   - GlowDeterminism  (2 TEST_CASEs: 3-run hash identity, fresh-renderer)
#   - TextGlowSmoke    (2 TEST_CASEs: 16:9 + 9:16 luminance preservation)
#
# Python scripts (require chronon3d_cli binary + rendered PNGs):
#   - tools/check_glow_ab.py luma|bbox (A/B frame comparison)
#   - tools/check_glow_temporal.py frames|ssim (60-frame sweep + MP4 SSIM)
#
# Per AGENTS.md §honesty "non segnare verde una suite che restituisce
# failure" + "no stime percentuali": when the chronon3d_cli binary is
# absent (e.g., on a VPS without vcpkg glm/magic_enum), the gate emits
# GATE_FAIL with a canonical rebuild hint.  On a working build host,
# all phases (ctest + Python scripts) must PASS for the gate to exit 0.
#
# AGENTS.md §"INFO-level diagnostic style": on clean PASS, emits one
# additional [INFO] line after the canonical GATE_PASS.
set -euo pipefail

GATE_NAME=check_glow_certification
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

# ── Phase 0: binary presence check ─────────────────────────────────────
BINARY_CANDIDATES=(
    "build/chronon/linux-content-dev/apps/chronon3d_cli/chronon3d_cli"
    "build/chronon/linux-fast-dev/apps/chronon3d_cli/chronon3d_cli"
    "build/manual-test/apps/chronon3d_cli/chronon3d_cli"
)
BINARY=""
for cand in "${BINARY_CANDIDATES[@]}"; do
    if [ -x "$REPO_ROOT/$cand" ]; then
        BINARY="$REPO_ROOT/$cand"
        break
    fi
done

if [ -z "$BINARY" ]; then
    echo "GATE_FAIL: chronon3d_cli binary not found in standard build paths" >&2
    echo "  checked: ${BINARY_CANDIDATES[*]}" >&2
    echo "  fix: cmake --preset linux-content-dev && cmake --build build/chronon/linux-content-dev -j\$(nproc)" >&2
    echo "  Per AGENTS.md §honesty: macchina-verifica deferred to working build host" >&2
    echo "  (this VPS lacks vcpkg glm/magic_enum + tmpfs quota for full project build)." >&2
    exit 1
fi

BUILD_DIR="$(dirname "$(dirname "$BINARY")")"

# ── Phase 1: ctest glow tests ──────────────────────────────────────────
echo "=== Phase 1: ctest glow tests ==="
FAILED_CTEST=0

GLOW_TESTS=(
    "GlowAcceptance"
    "GlowTemporal"
    "GlowDeterminism"
    "TextGlowSmoke"
)

for test_name in "${GLOW_TESTS[@]}"; do
    echo "--- ctest -R $test_name ---"
    if ctest --test-dir "$BUILD_DIR" -R "$test_name" --output-on-failure -j"$(nproc)"; then
        echo "  PASS: $test_name"
    else
        echo "  FAIL: $test_name" >&2
        FAILED_CTEST=1
    fi
done

if [ "$FAILED_CTEST" -ne 0 ]; then
    echo "GATE_FAIL: one or more ctest glow suites failed" >&2
    exit 1
fi

# ── Phase 2: Python A/B scripts ────────────────────────────────────────
echo "=== Phase 2: Python A/B scripts ==="

OUTPUT_DIR="$REPO_ROOT/output/glow_acceptance"
WITH_GLOW="$OUTPUT_DIR/with_glow.png"
NO_GLOW="$OUTPUT_DIR/no_glow.png"

# Render the A/B pair if not already present.
if [ ! -f "$WITH_GLOW" ] || [ ! -f "$NO_GLOW" ]; then
    echo "--- Rendering A/B pair ---"
    mkdir -p "$OUTPUT_DIR"
    "$BINARY" still ChrononGlowFinalAE --frame 15 -o "$WITH_GLOW" || {
        echo "GATE_FAIL: failed to render with-glow frame" >&2
        exit 1
    }
    "$BINARY" still ChrononGlowFinalAE_NoGlow --frame 15 -o "$NO_GLOW" || {
        echo "GATE_FAIL: failed to render no-glow frame" >&2
        exit 1
    }
fi

echo "--- check_glow_ab.py luma ---"
python3 "$SCRIPT_DIR/check_glow_ab.py" luma "$WITH_GLOW" "$NO_GLOW" || {
    echo "GATE_FAIL: luma contract violated" >&2
    exit 1
}

echo "--- check_glow_ab.py bbox ---"
python3 "$SCRIPT_DIR/check_glow_ab.py" bbox "$WITH_GLOW" "$NO_GLOW" || {
    echo "GATE_FAIL: bbox contract violated" >&2
    exit 1
}

# ── Phase 3: 60-frame temporal sweep ───────────────────────────────────
echo "=== Phase 3: 60-frame temporal sweep ==="

FRAMES_DIR="$OUTPUT_DIR/frames"
GLOW_MP4="$OUTPUT_DIR/glow.mp4"

if [ ! -f "$GLOW_MP4" ] || [ ! -d "$FRAMES_DIR" ]; then
    echo "--- Rendering 60-frame video ---"
    mkdir -p "$FRAMES_DIR"
    rm -rf "$FRAMES_DIR"/*
    "$BINARY" video ChrononGlowFinalAE \
        --start 0 --end 60 --fps 30 \
        --ffmpeg-mode png \
        --keep-frames \
        --frames-dir "$FRAMES_DIR" \
        -o "$GLOW_MP4" || {
        echo "GATE_FAIL: failed to render 60-frame video" >&2
        exit 1
    }
fi

echo "--- check_glow_temporal.py frames ---"
python3 "$SCRIPT_DIR/check_glow_temporal.py" frames "$FRAMES_DIR" || {
    echo "GATE_FAIL: temporal frames contract violated" >&2
    exit 1
}

# ── Phase 4: MP4 decode + SSIM ─────────────────────────────────────────
echo "=== Phase 4: MP4 decode + SSIM ==="

DECODED_DIR="$OUTPUT_DIR/decoded"
DECODED_COUNT=$(find "$DECODED_DIR" -name 'frame_*.png' 2>/dev/null | wc -l)

if [ "$DECODED_COUNT" -ne 60 ] && command -v ffmpeg &>/dev/null; then
    echo "--- Decoding MP4 to 60 PNG frames ---"
    mkdir -p "$DECODED_DIR"
    rm -rf "$DECODED_DIR"/*
    ffmpeg -v error -i "$GLOW_MP4" -vsync 0 "$DECODED_DIR/frame_%06d.png" || {
        echo "GATE_FAIL: ffmpeg decode failed" >&2
        exit 1
    }
fi

DECODED_COUNT=$(find "$DECODED_DIR" -name 'frame_*.png' 2>/dev/null | wc -l)
if [ "$DECODED_COUNT" -ne 60 ]; then
    echo "GATE_FAIL: expected 60 decoded frames, found $DECODED_COUNT" >&2
    echo "  fix: install ffmpeg with libx264 + SSIM filter support, then re-run" >&2
    exit 1
fi

echo "--- check_glow_temporal.py ssim ---"
python3 "$SCRIPT_DIR/check_glow_temporal.py" ssim "$GLOW_MP4" "$FRAMES_DIR" || {
    echo "GATE_FAIL: SSIM contract violated" >&2
    exit 1
}

# ── Phase 5: Determinism (raw PNG hashes) ───────────────────────────────
echo "=== Phase 5: Determinism (3-run sha256sum) ==="

for run in 1 2 3; do
    RUN_DIR="$OUTPUT_DIR/run_$run"
    if [ ! -f "$RUN_DIR.hashes" ]; then
        echo "--- Determinism run $run/3 ---"
        rm -rf "$RUN_DIR"
        "$BINARY" video ChrononGlowFinalAE \
            --start 0 --end 60 --fps 30 \
            --ffmpeg-mode png \
            --keep-frames \
            --frames-dir "$RUN_DIR" \
            -o "$RUN_DIR.mp4" || {
            echo "GATE_FAIL: determinism run $run render failed" >&2
            exit 1
        }
        find "$RUN_DIR" -name 'frame_*.png' -exec sha256sum {} \; \
            | awk '{print $1}' > "$RUN_DIR.hashes"
    fi
done

echo "--- Comparing run hashes ---"
diff "$OUTPUT_DIR/run_1.hashes" "$OUTPUT_DIR/run_2.hashes" || true  # diagnostic: show which frames differ
cmp "$OUTPUT_DIR/run_1.hashes" "$OUTPUT_DIR/run_2.hashes" || {
    echo "GATE_FAIL: run 1 != run 2 (non-deterministic render)" >&2
    exit 1
}
diff "$OUTPUT_DIR/run_1.hashes" "$OUTPUT_DIR/run_3.hashes" || true
cmp "$OUTPUT_DIR/run_1.hashes" "$OUTPUT_DIR/run_3.hashes" || {
    echo "GATE_FAIL: run 1 != run 3 (non-deterministic render)" >&2
    exit 1
}

# ── Gate PASS ───────────────────────────────────────────────────────────
echo "GATE_PASS: glow certification — all 5 phases PASS"
echo "[INFO] ${GATE_NAME}: 4 ctest suites + luma/bbox A/B + 60-frame temporal + MP4 SSIM + 3-run determinism ALL PASS"
exit 0
