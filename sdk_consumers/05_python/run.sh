#!/usr/bin/env bash
# Run the Python consumer against the installed Chronon3D package.
# The prefix is resolved from SDK_PREFIX / CHRONON3D_PREFIX (default /usr/local).
set -euo pipefail
cd "$(dirname "$0")"
PREFIX="${SDK_PREFIX:-${CHRONON3D_PREFIX:-/usr/local}}"
export CHRONON3D_PREFIX="$PREFIX"
export LD_LIBRARY_PATH="$PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
python3 main.py
