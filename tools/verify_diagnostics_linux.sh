#!/usr/bin/env bash
# Canonical Chronon3D diagnostics certification gate.
#
# This gate consumes the existing engine/SDK error channels through the single
# `chronon render` command. It intentionally rejects deprecated still/video
# aliases and generic parser/runtime messages as substitutes for structured
# render diagnostics.
#
# Exit codes:
#   0 = DIAGNOSTICS_FUNCTIONAL_PASS
#   1 = DIAGNOSTICS_FUNCTIONAL_FAIL
#   2 = DIAGNOSTICS_FUNCTIONAL_BLOCKED

set -uo pipefail

GATE_NAME="verify_diagnostics_linux"
ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$ROOT" || {
    echo "DIAGNOSTICS_FUNCTIONAL_BLOCKED"
    echo "  cannot enter repository root: $ROOT"
    exit 2
}

TMP_DIR="$(mktemp -d /tmp/chronon3d_diagnostics.XXXXXX)"
LOG_DIR="$TMP_DIR/logs"
OUT_DIR="$TMP_DIR/output"
mkdir -p "$LOG_DIR" "$OUT_DIR"
trap 'if [ "${CHRONON3D_DIAG_KEEP_LOGS:-0}" != "1" ]; then rm -rf "$TMP_DIR"; else echo "  logs kept at: $TMP_DIR"; fi' EXIT

PASS_COUNT=0
FAIL_COUNT=0
BLOCKED_COUNT=0

pass() {
    echo "  [PASS] $1"
    PASS_COUNT=$((PASS_COUNT + 1))
}

fail() {
    echo "  [FAIL] $1 — $2"
    FAIL_COUNT=$((FAIL_COUNT + 1))
}

blocked() {
    echo "  [BLOCKED] $1 — $2"
    BLOCKED_COUNT=$((BLOCKED_COUNT + 1))
}

find_cli() {
    if [ -n "${CHRONON3D_DIAG_CLI_BIN:-}" ] && [ -x "${CHRONON3D_DIAG_CLI_BIN}" ]; then
        printf '%s\n' "${CHRONON3D_DIAG_CLI_BIN}"
        return 0
    fi
    if [ -n "${CHRONON3D_CLI_BIN:-}" ] && [ -x "${CHRONON3D_CLI_BIN}" ]; then
        printf '%s\n' "${CHRONON3D_CLI_BIN}"
        return 0
    fi

    local candidate
    for candidate in \
        "$ROOT/build/chronon/linux-content-dev/chronon3d_cli" \
        "$ROOT/build/chronon/linux-ci/chronon3d_cli" \
        "$ROOT/build/chronon/linux-ci-full-validation/chronon3d_cli" \
        "$ROOT/build/chronon/linux-fast-dev/chronon3d_cli" \
        "$ROOT/build/manual-test/chronon3d_cli" \
        "$(command -v chronon3d_cli 2>/dev/null || true)"; do
        if [ -n "$candidate" ] && [ -x "$candidate" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    return 1
}

# Existing taxonomy tokens. These are not a second error hierarchy: the gate
# merely proves that each required class maps to an engine/SDK error already
# present in the source tree.
STATIC_MATRIX=(
    'FontNotFound|MissingFontEngine|TEXT_FONT_NOT_FOUND'
    'AssetNotFound|AssetResolutionFailure|ASSET_NOT_FOUND'
    'DecodeFailed|DecodeFailed|DECODE_FAILED'
    'InvalidCameraDescriptor|InvalidCameraProgram|INVALID_CAMERA_DESCRIPTOR'
    'CameraTargetNotFound|TargetNotFound|CAMERA_TARGET_NOT_FOUND'
    'FrameDimensionExceeded|MaxDimensionExceeded|FRAME_DIMENSION_EXCEEDED'
    'MemoryBudgetExceeded|InsufficientMemory|MEMORY_BUDGET_EXCEEDED'
    'OutputOpenFailed|OutputOpenFailed|OUTPUT_OPEN_FAILED'
    'VideoEncoderFailed|EncoderFailed|VIDEO_ENCODER_FAILED'
    'InvalidTimeRange|InvalidRange|INVALID_TIME_RANGE'
)

SCAN_PATHS=()
for path in include src apps tests; do
    [ -e "$path" ] && SCAN_PATHS+=("$path")
done

echo "=============================================="
echo " verify_diagnostics_linux.sh"
echo " canonical render error certification"
echo "=============================================="
echo ""

echo "== 1. Architecture prerequisites =="
for gate in tools/check_no_legacy_render_cli.sh tools/check_architecture_boundaries.sh tools/check_test_hygiene.sh; do
    if [ ! -f "$gate" ]; then
        blocked "$(basename "$gate")" "script missing"
        continue
    fi
    if bash "$gate" >/dev/null 2>&1; then
        pass "$(basename "$gate")"
    else
        fail "$(basename "$gate")" "gate returned non-zero"
    fi
done

echo ""
echo "== 2. Existing error taxonomy =="
for row in "${STATIC_MATRIX[@]}"; do
    IFS='|' read -r class token stable_code <<< "$row"
    if grep -RqsE --exclude-dir=.git --exclude-dir=build -- "$token" "${SCAN_PATHS[@]}"; then
        pass "$class maps to existing token $token"
    else
        fail "$class" "existing engine/SDK token not found: $token"
    fi

    # Stable CLI codes are presentation constants and must be discoverable in
    # the canonical formatter once the runtime path supports the class.
    if grep -RqsF --exclude-dir=.git --exclude-dir=build -- "$stable_code" apps include src 2>/dev/null; then
        pass "$class stable CLI code $stable_code"
    else
        fail "$class stable CLI code" "$stable_code not exposed by canonical formatter"
    fi
done

if ! CLI_BIN="$(find_cli)"; then
    echo ""
    blocked "runtime diagnostics" "chronon3d_cli not found; set CHRONON3D_DIAG_CLI_BIN or build the CLI"
else
    echo ""
    echo "== 3. Runtime canonical command surface =="
    if CHRONON3D_CLI_BIN="$CLI_BIN" bash tools/verify_cli_render_surface_linux.sh >/dev/null 2>&1; then
        pass "canonical CLI command surface"
    else
        rc=$?
        if [ "$rc" -eq 2 ]; then
            blocked "canonical CLI command surface" "runtime surface gate blocked"
        else
            fail "canonical CLI command surface" "runtime surface gate failed"
        fi
    fi

    FORBIDDEN_MARKERS=(
        'something failed'
        'fallback frame'
        'black frame'
        'continue on error'
        'fallback to silence'
    )

    run_case() {
        local name="$1"
        local expected_code="$2"
        local required_fields_csv="$3"
        shift 3

        local stdout_log="$LOG_DIR/${name}.stdout"
        local stderr_log="$LOG_DIR/${name}.stderr"
        local rc=0

        "$@" >"$stdout_log" 2>"$stderr_log" || rc=$?

        local body
        body="$(cat "$stderr_log" 2>/dev/null || true)"
        local errors=()

        if [ "$rc" -eq 0 ]; then
            errors+=("spurious exit 0")
        fi
        if [ -z "$body" ]; then
            errors+=("empty stderr")
        fi
        if ! grep -qF -- "$expected_code" "$stderr_log" 2>/dev/null; then
            errors+=("missing stable code $expected_code")
        fi

        local field
        IFS=',' read -r -a required_fields <<< "$required_fields_csv"
        for field in "${required_fields[@]}"; do
            [ -z "$field" ] && continue
            if ! grep -qiF -- "$field" "$stderr_log" 2>/dev/null; then
                errors+=("missing field $field")
            fi
        done

        local marker
        for marker in "${FORBIDDEN_MARKERS[@]}"; do
            if grep -qiF -- "$marker" "$stderr_log" 2>/dev/null; then
                errors+=("forbidden generic marker: $marker")
            fi
        done

        if [ "${#errors[@]}" -eq 0 ]; then
            pass "runtime[$name] code=$expected_code"
        else
            local preview
            preview="$(tr '\n' ' ' < "$stderr_log" 2>/dev/null | head -c 240)"
            fail "runtime[$name]" "${errors[*]}; stderr=${preview}"
        fi
    }

    echo ""
    echo "== 4. Runtime 10-class structured matrix =="

    run_case FontNotFound TEXT_FONT_NOT_FOUND \
        'Composition:,Frame:,Asset:,Details:,Fix:' \
        "$CLI_BIN" render CertMissingFont --frame 0 -o "$OUT_DIR/font.png"

    run_case AssetNotFound ASSET_NOT_FOUND \
        'Composition:,Frame:,Asset:,Details:,Fix:' \
        "$CLI_BIN" render AssetNotFound --frame 0 -o "$OUT_DIR/asset.png"

    run_case DecodeFailed DECODE_FAILED \
        'Composition:,Frame:,Asset:,Details:,Fix:' \
        "$CLI_BIN" render CertCorruptPNG --frame 0 -o "$OUT_DIR/decode.png"

    run_case InvalidCameraDescriptor INVALID_CAMERA_DESCRIPTOR \
        'Composition:,Frame:,Details:,Fix:' \
        "$CLI_BIN" render CertInvalidCamera --frame 0 -o "$OUT_DIR/camera.png"

    run_case CameraTargetNotFound CAMERA_TARGET_NOT_FOUND \
        'Composition:,Frame:,Details:,Fix:' \
        "$CLI_BIN" render CertCameraTargetMissing --frame 0 -o "$OUT_DIR/camera_target.png"

    run_case FrameDimensionExceeded FRAME_DIMENSION_EXCEEDED \
        'Composition:,Frame:,Details:,Fix:' \
        "$CLI_BIN" render DimensionCheck --frame 0 -o "$OUT_DIR/dimension.png"

    run_case MemoryBudgetExceeded MEMORY_BUDGET_EXCEEDED \
        'Composition:,Frame:,Details:,Fix:' \
        "$CLI_BIN" render MemoryBudgetCheck --frame 0 -o "$OUT_DIR/memory.png"

    run_case OutputOpenFailed OUTPUT_OPEN_FAILED \
        'Composition:,Frame:,Details:,Fix:' \
        "$CLI_BIN" render OutputCheck --frame 0 -o /proc/chronon3d_diagnostics_output.png

    run_case VideoEncoderFailed VIDEO_ENCODER_FAILED \
        'Composition:,Frame:,Details:,Fix:' \
        "$CLI_BIN" render CodecCheck --frames 0-1 -o "$OUT_DIR/encoder.mp4" --codec nonexistent_codec_xyz

    run_case InvalidTimeRange INVALID_TIME_RANGE \
        'Composition:,Details:,Fix:' \
        "$CLI_BIN" render SequenceTimeRangeCheck --frames 100-50 -o "$OUT_DIR/frame_####.png"

    echo ""
    echo "== 5. Formatter baseline =="
    run_case UnknownComposition UNKNOWN_COMPOSITION \
        'Composition:,Details:,Fix:' \
        "$CLI_BIN" render __ChrononMissingComposition__ --frame 0 -o "$OUT_DIR/missing.png"
fi

echo ""
echo "=============================================="
echo " VERDICT"
echo "=============================================="
echo "  PASS:    $PASS_COUNT"
echo "  FAIL:    $FAIL_COUNT"
echo "  BLOCKED: $BLOCKED_COUNT"
echo ""

if [ "$FAIL_COUNT" -gt 0 ]; then
    echo "DIAGNOSTICS_FUNCTIONAL_FAIL"
    echo "  structured error contract has $FAIL_COUNT failure(s)"
    exit 1
fi

if [ "$BLOCKED_COUNT" -gt 0 ]; then
    echo "DIAGNOSTICS_FUNCTIONAL_BLOCKED"
    echo "  runtime or prerequisite verification is blocked"
    exit 2
fi

echo "DIAGNOSTICS_FUNCTIONAL_PASS"
echo "[INFO] ${GATE_NAME}: canonical render path passed the 10-class structured error matrix"
exit 0
