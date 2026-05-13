#!/usr/bin/env bash
# Build script for Chronon3d on Linux.
# Dependencies are managed via vcpkg manifest mode (vcpkg.json).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_DIR"

# ── System tools ──────────────────────────────────────────────────────────────
if command -v apt-get >/dev/null 2>&1; then
  missing=()
  command -v cmake   >/dev/null 2>&1 || missing+=(cmake)
  command -v ninja   >/dev/null 2>&1 || missing+=(ninja-build)
  command -v git     >/dev/null 2>&1 || missing+=(git)
  command -v curl    >/dev/null 2>&1 || missing+=(curl)
  if [[ ${#missing[@]} -gt 0 ]]; then
    sudo apt-get update -qq
    sudo apt-get install -y --no-install-recommends "${missing[@]}" \
      build-essential pkg-config zip unzip tar
  fi
fi

# ── vcpkg ─────────────────────────────────────────────────────────────────────
VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
if [[ ! -x "$VCPKG_ROOT/vcpkg" ]]; then
  echo "vcpkg not found at $VCPKG_ROOT"
  echo "Cloning and bootstrapping vcpkg..."
  git clone https://github.com/microsoft/vcpkg "$VCPKG_ROOT"
  "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
fi

# Fetch full history so the baseline commit in vcpkg.json is reachable.
if git -C "$VCPKG_ROOT" rev-parse --is-shallow-repository 2>/dev/null | grep -q true; then
  echo "Fetching full vcpkg history for baseline..."
  git -C "$VCPKG_ROOT" fetch --unshallow
fi

export VCPKG_ROOT

# ── Compiler cache (optional) ─────────────────────────────────────────────────
if command -v sccache >/dev/null 2>&1; then
  export CMAKE_C_COMPILER_LAUNCHER=sccache
  export CMAKE_CXX_COMPILER_LAUNCHER=sccache
elif command -v ccache >/dev/null 2>&1; then
  export CMAKE_C_COMPILER_LAUNCHER=ccache
  export CMAKE_CXX_COMPILER_LAUNCHER=ccache
fi

# ── Configure + Build ─────────────────────────────────────────────────────────
# vcpkg.json drives dependency installation automatically.
PRESET="${CHRONON_PRESET:-linux-release}"
cmake --preset "$PRESET"
cmake --build "build/chronon/$PRESET" -j"${CHRONON_JOBS:-$(nproc)}"

echo ""
echo "Build complete. Binaries in build/chronon/$PRESET/"
echo "  chronon3d_cli   — CLI for rendering and video export"
echo "  chronon3d_tests — doctest test suite"
