#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_DIR"

PRESET="${CHRONON_PRESET:-linux-release}"
BUILD_DIR="build/chronon/$PRESET"
BIN="$BUILD_DIR/apps/chronon3d_cli/chronon3d_cli"
SUITE="tools/visual_quality_suite.py"

if [[ ! -x "$BIN" ]]; then
  echo "Missing binary: $BIN"
  echo "Build it first with: cmake --preset $PRESET && cmake --build $BUILD_DIR -j${CHRONON_JOBS:-$(nproc)}"
  exit 1
fi

echo "Rendering and validating camera shots..."
python3 "$SUITE" \
  --executable "$BIN" \
  --skip-pipeline \
  --camera-template OrbitCameraTest ExtremePerspectiveTest HeroTextFrontTest ZStackParallaxTest \
  --camera-output-dir output/camera_smoke \
  --camera-overlay-dir output/camera_smoke_overlay

echo "Done."
echo "Telemetry DB: $HOME/.chronon3d/telemetry/chronon3d_render_history.sqlite"
echo "Camera renders:"
echo "  output/camera_smoke/OrbitCameraTest.png"
echo "  output/camera_smoke/ExtremePerspectiveTest.png"
echo "  output/camera_smoke/HeroTextFrontTest.png"
echo "  output/camera_smoke/ZStackParallaxTest.png"
echo "Camera overlays:"
echo "  output/camera_smoke_overlay/OrbitCameraTest.png"
echo "  output/camera_smoke_overlay/ExtremePerspectiveTest.png"
echo "  output/camera_smoke_overlay/HeroTextFrontTest.png"
echo "  output/camera_smoke_overlay/ZStackParallaxTest.png"
echo "Open the dashboard after refresh:"
echo "  http://51.91.11.36:5173/"
