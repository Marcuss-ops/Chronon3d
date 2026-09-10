#!/usr/bin/env bash
# Build + run the Python consumer against the installed Chronon3D package.
# The prefix is resolved from SDK_PREFIX / CHRONON3D_PREFIX (default /usr/local).
# The Python consumer needs no build step: ctypes loads libchronon3d_c.so
# directly. This matches the convention of 03_go/run.sh and 04_rust/run.sh.
set -euo pipefail
cd "$(dirname "$0")"
PREFIX="${SDK_PREFIX:-${CHRONON3D_PREFIX:-/usr/local}}"
LIB="$PREFIX/lib/libchronon3d_c.so"
if [[ ! -f "$LIB" ]]; then
    echo "libchronon3d_c.so not found at $LIB (set SDK_PREFIX to the installed Chronon3D prefix)" >&2
    exit 1
fi
export LD_LIBRARY_PATH="$PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec python3 "$PWD/probe.py" "$LIB" consume
