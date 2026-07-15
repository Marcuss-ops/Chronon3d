#!/usr/bin/env bash
# Working-build-host smoke test for the canonical Chronon3D CLI surface.
# Verifies the runtime command registry after removal of still/video aliases.

set -euo pipefail

GATE_NAME="verify_cli_render_surface_linux"
ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

find_cli() {
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

if ! CLI_BIN="$(find_cli)"; then
    echo "CLI_RENDER_SURFACE_BLOCKED"
    echo "  chronon3d_cli not found; set CHRONON3D_CLI_BIN or build the CLI"
    exit 2
fi

TMP_DIR="$(mktemp -d /tmp/chronon3d_cli_surface.XXXXXX)"
trap 'rm -rf "$TMP_DIR"' EXIT

TOP_HELP="$TMP_DIR/top_help.txt"
RENDER_HELP="$TMP_DIR/render_help.txt"
PREVIEW_HELP="$TMP_DIR/preview_help.txt"
CREATE_HELP="$TMP_DIR/create_help.txt"

if ! "$CLI_BIN" --help >"$TOP_HELP" 2>&1; then
    echo "CLI_RENDER_SURFACE_FAIL"
    echo "  top-level --help failed"
    cat "$TOP_HELP"
    exit 1
fi

require_command() {
    local name="$1"
    if ! grep -Eq "^[[:space:]]+${name}([[:space:]]|$)" "$TOP_HELP"; then
        echo "  [FAIL] required command missing: $name"
        return 1
    fi
    echo "  [PASS] command present: $name"
}

forbid_command() {
    local name="$1"
    if grep -Eq "^[[:space:]]+${name}([[:space:]]|$)" "$TOP_HELP"; then
        echo "  [FAIL] retired command exposed: $name"
        return 1
    fi
    echo "  [PASS] retired command absent: $name"
}

FAILED=0
require_command render || FAILED=1
require_command preview || FAILED=1
require_command create || FAILED=1
forbid_command still || FAILED=1
forbid_command video || FAILED=1

if ! "$CLI_BIN" render --help >"$RENDER_HELP" 2>&1; then
    echo "  [FAIL] render --help failed"
    FAILED=1
else
    for option in --frame --frames --output; do
        if grep -q -- "$option" "$RENDER_HELP"; then
            echo "  [PASS] render option present: $option"
        else
            echo "  [FAIL] render option missing: $option"
            FAILED=1
        fi
    done
fi

if ! "$CLI_BIN" preview --help >"$PREVIEW_HELP" 2>&1; then
    echo "  [FAIL] preview --help failed"
    FAILED=1
else
    for option in --frames --contact-sheet --output-dir; do
        if grep -q -- "$option" "$PREVIEW_HELP"; then
            echo "  [PASS] preview option present: $option"
        else
            echo "  [FAIL] preview option missing: $option"
            FAILED=1
        fi
    done
fi

if ! "$CLI_BIN" create --help >"$CREATE_HELP" 2>&1; then
    echo "  [FAIL] create --help failed"
    FAILED=1
else
    if grep -q -- '--template' "$CREATE_HELP"; then
        echo "  [PASS] create template selector present"
    else
        echo "  [FAIL] create --template missing"
        FAILED=1
    fi
fi

if [ "$FAILED" -ne 0 ]; then
    echo "CLI_RENDER_SURFACE_FAIL"
    exit 1
fi

echo "CLI_RENDER_SURFACE_PASS"
echo "[INFO] ${GATE_NAME}: render is canonical; preview/create present; still/video absent"
exit 0
