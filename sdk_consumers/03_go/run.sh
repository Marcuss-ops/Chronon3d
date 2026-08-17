#!/usr/bin/env bash
# Build + run the Go consumer against the installed Chronon3D package.
# The prefix is resolved from SDK_PREFIX / CHRONON3D_PREFIX (default /usr/local).
set -euo pipefail
cd "$(dirname "$0")"
PREFIX="${SDK_PREFIX:-${CHRONON3D_PREFIX:-/usr/local}}"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export CGO_ENABLED=1
go run .
