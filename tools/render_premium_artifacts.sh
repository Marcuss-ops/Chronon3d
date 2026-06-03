#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_DIR"

PRESET="${CHRONON_PRESET:-linux-release}"
BUILD_DIR="build/chronon/$PRESET"
BIN="$BUILD_DIR/apps/chronon3d_cli/chronon3d_cli"

if [[ ! -x "$BIN" ]]; then
  echo "Missing binary: $BIN"
  echo "Build it first with: cmake --preset $PRESET && cmake --build $BUILD_DIR -j${CHRONON_JOBS:-$(nproc)}"
  exit 1
fi

render() {
  local comp="$1"
  local output="$2"
  echo "Rendering $comp -> $output"
  "$BIN" render "$comp" --frame 0 --report -o "$output"
}

render "PremiumThumbnailSaaSBlue" "output/premium_thumbnail_saas_blue.png"
render "PremiumThumbnailButterySmooth" "output/premium_thumbnail_buttery_smooth.png"
render "TextPremiumHeroSaaSBlue" "output/text_premium_hero_saas_blue.png"

echo "Done."
echo "Telemetry DB: $HOME/.chronon3d/telemetry/chronon3d_render_history.sqlite"
echo "Open the dashboard after refresh:"
echo "  http://51.91.11.36:5173/"
