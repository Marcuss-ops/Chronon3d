#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_DIR"

if [[ "${CHRONON_SKIP_DEPS:-0}" != "1" ]]; then
  if command -v apt-get >/dev/null 2>&1; then
    if ! command -v cmake >/dev/null 2>&1 || ! command -v ninja >/dev/null 2>&1 || ! command -v pkg-config >/dev/null 2>&1; then
      sudo apt-get update
      sudo apt-get install -y build-essential cmake ninja-build ccache pkg-config git curl zip unzip tar
    fi
  fi
fi

VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
if [[ ! -x "$VCPKG_ROOT/vcpkg" ]]; then
  echo "vcpkg not found at $VCPKG_ROOT/vcpkg"
  echo "Clone and bootstrap vcpkg first, or set VCPKG_ROOT."
  exit 1
fi

COMPILER_LAUNCHER=""
if command -v sccache >/dev/null 2>&1; then
  COMPILER_LAUNCHER="$(command -v sccache)"
elif command -v ccache >/dev/null 2>&1; then
  COMPILER_LAUNCHER="$(command -v ccache)"
fi
if [[ -n "$COMPILER_LAUNCHER" ]]; then
  export CHRONON_COMPILER_LAUNCHER="$COMPILER_LAUNCHER"
fi

packages=(
  sdl2
  spdlog
  fmt
  entt
  meshoptimizer
  stb
  nlohmann-json
  magic-enum
  ffmpeg
  tracy
  mimalloc
  taskflow
  concurrentqueue
  abseil
  glm
  simdjson
  xxhash
  highway
  doctest
)

if [[ "${CHRONON_SKIP_DEPS:-0}" != "1" ]]; then
  "$VCPKG_ROOT/vcpkg" install --classic --triplet x64-linux --x-install-root "$PROJECT_DIR/vcpkg_installed" --no-print-usage "${packages[@]}"
fi

cmake --preset linux-release
cmake --build --preset linux
