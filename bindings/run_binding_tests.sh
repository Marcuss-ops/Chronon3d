#!/usr/bin/env bash
# Run the Python and Go binding test suites against a built libchronon3d_c.
#
#   CHRONON3D_LIB=/path/to/libchronon3d_c.so bindings/run_binding_tests.sh [python|go|all]
#
# If CHRONON3D_LIB is unset, the script looks in build/chronon/*/ for the
# library.  Go is built with -tags chronon3d_manual and CGO flags derived from
# CHRONON3D_LIB + the in-tree include/ dir (no installed chronon3d.pc needed).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
WHICH="${1:-all}"

if [[ -z "${CHRONON3D_LIB:-}" ]]; then
    CHRONON3D_LIB="$(find "$ROOT/build" -name 'libchronon3d_c.so' -print -quit 2>/dev/null || true)"
fi
if [[ -z "${CHRONON3D_LIB}" || ! -e "${CHRONON3D_LIB}" ]]; then
    echo "libchronon3d_c.so not found; build target chronon3d_c with -DCHRONON3D_BUILD_C_API=ON and set CHRONON3D_LIB" >&2
    exit 77
fi
CHRONON3D_LIB="$(cd "$(dirname "$CHRONON3D_LIB")" && pwd)/$(basename "$CHRONON3D_LIB")"
export CHRONON3D_LIB
LIBDIR="$(dirname "$CHRONON3D_LIB")"
export LD_LIBRARY_PATH="$LIBDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
echo "using CHRONON3D_LIB=$CHRONON3D_LIB"

rc=0
ran=0
if [[ "$WHICH" == python || "$WHICH" == all ]]; then
    PY="${PYTHON:-python3}"
    if "$PY" -c 'import pytest' 2>/dev/null; then
        ran=1; (cd "$HERE/python" && "$PY" -m pytest -q) || rc=1
    else
        ran=1; (cd "$HERE/python" && PYTHONPATH=. "$PY" -m unittest discover -s tests -p "test_*.py" -v) || rc=1
    fi
fi
if [[ "$WHICH" == go || "$WHICH" == all ]]; then
    if command -v go >/dev/null; then
        ran=1
        (cd "$HERE/go/chronon3d" && \
         CGO_ENABLED=1 CGO_CFLAGS="-I$ROOT/include" \
         CGO_LDFLAGS="-L$LIBDIR -lchronon3d_c -Wl,-rpath,$LIBDIR" \
         go test -tags chronon3d_manual -count=1 -v ./...) || rc=1
    else
        echo "go not installed; skipping go bindings" >&2
    fi
fi
[[ $ran == 0 ]] && exit 77
exit $rc
