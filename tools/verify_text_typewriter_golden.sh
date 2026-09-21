#!/usr/bin/env bash
# Blocking multi-frame text golden gate.
#
# The test renders the real selector-driven typewriter plan at frame 0, reveal
# midpoint, completion, completion+1 and end. It checks non-zero luminance,
# monotonic reveal and completion stability; a single still cannot certify the
# completion+1 transition.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
BUILD_DIR="${1:-${CHRONON3D_TEXT_GOLDEN_BUILD_DIR:-${ROOT}/build/chronon/linux-fast-dev}}"
TARGET=chronon3d_video_flicker_fps_tests
TEST_BIN="${BUILD_DIR}/tests/${TARGET}"
TEST_CASE='TextGoldenVideo.SelectorTypewriter_SampledFramesStayVisible'

if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    echo "verify_text_typewriter_golden: missing configured build ${BUILD_DIR}" >&2
    exit 1
fi

echo "verify_text_typewriter_golden: clean target build ${TARGET}"
cmake --build "${BUILD_DIR}" --target "${TARGET}" -j "${JOBS:-$(nproc)}"

if [[ ! -x "${TEST_BIN}" ]]; then
    echo "verify_text_typewriter_golden: missing test binary ${TEST_BIN}" >&2
    exit 1
fi

(cd "${ROOT}" && "${TEST_BIN}" --test-case="${TEST_CASE}" --no-skip)
echo "verify_text_typewriter_golden: PASS"
