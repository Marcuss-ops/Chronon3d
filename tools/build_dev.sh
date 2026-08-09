#!/usr/bin/env bash
# Chronon3D daily development orchestrator.
# It deliberately delegates compilation to build-fast.sh and test selection
# to the CTest `dev-fast` label generated from CHRONON3D_FAST_TEST_DEPS.
set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR_EXPLICIT="${BUILD_DIR_OVERRIDE+x}"
BUILD_DIR="${BUILD_DIR_OVERRIDE:-${ROOT_DIR}/.tmp/chronon-builds/linux-fast-dev}"
JOBS="${JOBS:-8}"
KEEP_ARTIFACTS=0
MODE="default"

usage() {
    cat <<EOF
Usage: ./tools/build_dev.sh [--clean|--build-only|--test-only|--keep-artifacts]

Daily flow: configure/reuse + build-fast.sh + CTest label dev-fast + cleanup.
Environment: JOBS, BUILD_DIR_OVERRIDE, CCACHE_DIR.
EOF
}

while (($# > 0)); do
    case "$1" in
        --clean) MODE="clean" ;;
        --build-only) MODE="build-only" ;;
        --test-only) MODE="test-only" ;;
        --keep-artifacts) KEEP_ARTIFACTS=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "BUILD_DEV_FAIL"; echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

if [[ "$MODE" == "clean" ]]; then
    case "$BUILD_DIR" in
        "$ROOT_DIR/.tmp/chronon-builds/"*|/tmp/chronon-builds/*) ;;
        *)
            echo "BUILD_DEV_FAIL"
            echo "Refusing --clean outside a known generated build directory: $BUILD_DIR" >&2
            exit 2
            ;;
    esac
    rm -rf -- "$BUILD_DIR"
    MODE="default"
fi

cd "$ROOT_DIR"
START_NS="$(date +%s%N)"
PREVIOUS_PROFILE="COLD_CONFIGURE"
[[ -f "$BUILD_DIR/build.ninja" ]] && PREVIOUS_PROFILE="WARM_INCREMENTAL"

size_or_zero() {
    local path="$1"
    [[ -e "$path" ]] && du -sh "$path" 2>/dev/null | awk '{print $1}' || echo "0"
}

run_phase() {
    local phase="$1"
    shift
    echo "[$phase] $*"
    if ! "$@"; then
        echo "BUILD_DEV_FAIL"
        echo "Phase: $phase"
        echo "Command: $*"
        exit 1
    fi
}

echo "[1/5] Preflight"
echo "      preset: linux-fast-dev"
echo "      jobs: $JOBS"
CCACHE_PATH="${CCACHE_DIR:-${ROOT_DIR}/.ccache}"
if command -v ccache >/dev/null 2>&1; then
    echo "      ccache: enabled ($CCACHE_PATH)"
else
    echo "      ccache: unavailable (compiler launcher may fail at configure)"
fi
echo "      build dir: $BUILD_DIR"
echo "      build space: $(size_or_zero "$BUILD_DIR")"
echo "      free disk: $(df -h "$ROOT_DIR" | awk 'NR==2 {print $4}')"
echo "      profile: $PREVIOUS_PROFILE"

if [[ "$MODE" == "test-only" ]]; then
    echo "[2/5] Configure skipped (--test-only)"
    echo "[3/5] Build skipped (--test-only)"
else
    echo "[2/5] Configure + build (delegated to build-fast.sh)"
    if [[ -n "$BUILD_DIR_EXPLICIT" ]]; then
        run_phase "3/5 Build" env JOBS="$JOBS" BUILD_DIR_OVERRIDE="$BUILD_DIR" bash "$ROOT_DIR/build-fast.sh"
    else
        run_phase "3/5 Build" env JOBS="$JOBS" bash "$ROOT_DIR/build-fast.sh"
    fi
fi

if [[ "$MODE" != "build-only" ]]; then
    echo "[4/5] Fast tests"
    run_phase "4/5 Fast tests" ctest --test-dir "$BUILD_DIR" -L dev-fast --output-on-failure
else
    echo "[4/5] Fast tests skipped (--build-only)"
fi

if [[ "$KEEP_ARTIFACTS" -eq 0 ]]; then
    echo "[5/5] Cleanup"
    reclaimed=0
    for artifact_dir in "$ROOT_DIR/.tmp/test-artifacts" "$ROOT_DIR/.tmp/test-output" "$ROOT_DIR/.tmp/certification"; do
        if [[ -d "$artifact_dir" ]]; then
            before=$(du -sk "$artifact_dir" 2>/dev/null | awk '{print $1}')
            rm -rf -- "$artifact_dir"
            reclaimed=$((reclaimed + before))
        fi
    done
    echo "      temporary artifacts removed: $((reclaimed / 1024)) MB reclaimed"
else
    echo "[5/5] Cleanup skipped (--keep-artifacts)"
fi

END_NS="$(date +%s%N)"
TOTAL_MS=$(( (END_NS - START_NS) / 1000000 ))
echo "BUILD_DEV_PASS"
echo "Total: $((TOTAL_MS / 1000)).$((TOTAL_MS % 1000))s"
