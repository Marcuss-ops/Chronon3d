#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# run.sh — render the 6 ordered overlay tests as single stills.
#
#   Test 1  B06 existing composition (Image+Text overlay)          [software]
#   Test 2  background + important phrase                          [semantic]
#   Test 3  background + important word (x4)                       [semantic]
#   Test 4  background + image overlay                             [semantic]
#   Test 5  real 10s scene (phrase→word→image→phrase→image)        [semantic]
#   Test 6  10 variants on an identical background                 [semantic]
#
# Each semantic test goes through the canonical pipeline:
#   overlay events ─▶ chronon script ─▶ RenderPlan JSON ─▶ chronon render-plan
#
# Usage:
#   bash examples/script_overlay_tests/run.sh
#
# Env:
#   CHRONON3D_CLI_BIN   override the CLI binary path
#   CHRONON3D_TEST_OUT  override the output directory (default /tmp/chronon_script_tests)
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SUITE_DIR="$ROOT/examples/script_overlay_tests"
BIN="${CHRONON3D_CLI_BIN:-$ROOT/.tmp/chronon-builds/linux-fast-dev/apps/chronon3d_cli/chronon3d_cli}"
OUT="${CHRONON3D_TEST_OUT:-/tmp/chronon_script_tests}"

if [[ ! -x "$BIN" ]]; then
    echo "ERROR: CLI binary not found at $BIN" >&2
    echo "       Build it (e.g. ./build-fast.sh cli) or set CHRONON3D_CLI_BIN." >&2
    exit 2
fi

mkdir -p "$OUT"
pass=0
fail=0

check_png() {
    local path="$1"
    if [[ -s "$path" ]] && file "$path" | grep -q 'PNG image data'; then
        printf '    PASS %-28s %s bytes\n' "$(basename "$path")" "$(stat -c%s "$path")"
        pass=$((pass + 1))
    else
        printf '    FAIL %s (missing or not a PNG)\n' "$path"
        fail=$((fail + 1))
    fi
}

# semantic_test <name> <json> <frame> [frame ...]
#   Render the given semantic script and capture one still per listed frame.
semantic_test() {
    local name="$1"
    local json="$2"
    shift 2
    local plan="$OUT/$name.plan.json"

    if ! "$BIN" script "$SUITE_DIR/$json" -o "$plan" >/dev/null 2>&1; then
        echo "    FAIL $name: chronon script failed"
        fail=$((fail + 1))
        return
    fi
    for frame in "$@"; do
        local out="$OUT/${name}_f${frame}.png"
        if "$BIN" render-plan --input "$plan" --start-frame "$frame" --end-frame "$frame" \
              --assets-root "$ROOT" --output "$out" >/dev/null 2>&1; then
            check_png "$out"
        else
            echo "    FAIL $name f$frame: render-plan failed"
            fail=$((fail + 1))
        fi
    done
}

echo "=== Test 1: BenchB06_VideoOverlay1080p (Image+Text overlay) ==="
"$BIN" render BenchB06_VideoOverlay1080p --frame 30 --backend software \
    -o "$OUT/t1_b06_f30.png" >/dev/null 2>&1 \
    && check_png "$OUT/t1_b06_f30.png" \
    || { echo "    FAIL Test 1: render failed"; fail=$((fail + 1)); }

echo "=== Test 2: background + important phrase ==="
semantic_test test2_background_phrase test2_background_phrase.json 30

echo "=== Test 3: background + important word (x4) ==="
semantic_test test3_background_word test3_background_word.json 30 120

echo "=== Test 4: background + image overlay ==="
semantic_test test4_background_image test4_background_image.json 60

echo "=== Test 5: real 10s scene (phrase→word→image→phrase→image) ==="
semantic_test test5_scene_10s test5_scene_10s.json 30 90 160 220 270

echo "=== Test 6: 10 variants on an identical background ==="
semantic_test test6_ten_variants test6_ten_variants.json 10 150 290

echo ""
echo "Result: $pass PASS / $fail FAIL"
[[ "$fail" -eq 0 ]]
