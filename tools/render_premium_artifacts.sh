#!/usr/bin/env bash
set -euo pipefail

# ═══════════════════════════════════════════════════════════════════════════
# tools/render_premium_artifacts.sh
#
# Renders the "premium" production compositions against the current
# chronon3d_cli binary, then (PR 6) renders the authoring-DSL migration
# validator into a sibling directory and asserts pixel-equivalence via
# tools/compare_pngs.py.
#
# Composition selection:
#   • PremiumThumbnailButterySmooth (brace-init)
#   • PremiumThumbnailSaaSBlue     (brace-init)
#   • TextPremiumHeroSaaSBlue      (brace-init)
#   • PremiumThumbnailSaaSBlueAuthored (PR 6 authoring-DSL validation)
#
# Output layout:
#   output/brace_init/<composition>.png   — current production baseline
#   output/authored/<composition>.png     — authoring-DSL validator
#   output/diff_report.txt                — compare_pngs.py stdout
#
# Exit codes:
#   0 — all renders successful AND pixel-equivalence asserted (or skipped)
#   1 — render or compare failure
#   2 — compare_pngs.py reported mismatches
# ═══════════════════════════════════════════════════════════════════════════

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_DIR"

PRESET="${CHRONON_PRESET:-linux-fast-dev-release}"
BUILD_DIR="build/chronon/$PRESET"
BIN="$BUILD_DIR/apps/chronon3d_cli/chronon3d_cli"

if [[ ! -x "$BIN" ]]; then
  # Fall back to linux-fast-dev (DEBUG) when no release artifact is present.
  PRESET="linux-fast-dev"
  BUILD_DIR="build/chronon/$PRESET"
  BIN="$BUILD_DIR/apps/chronon3d_cli/chronon3d_cli"
fi

if [[ ! -x "$BIN" ]]; then
  echo "Missing binary: $BIN"
  echo "Build it first with: cmake --preset $PRESET && cmake --build $BUILD_DIR -j\${CHRONON_JOBS:-\$(nproc)}"
  echo "  or for a faster path: ./build-fast.sh release"
  exit 1
fi

BRACE_DIR="output/brace_init"
AUTHORED_DIR="output/authored"
mkdir -p "$BRACE_DIR" "$AUTHORED_DIR"

render() {
  local comp="$1"
  local output="$2"
  echo "▸ Rendering $comp -> $output"
  "$BIN" render "$comp" --frame 0 --report -o "$output"
}

# ── Brace-init baseline (PR 5 era) ─────────────────────────────────────────
render "PremiumThumbnailSaaSBlue"        "$BRACE_DIR/premium_thumbnail_saas_blue.png"
render "PremiumThumbnailButterySmooth"  "$BRACE_DIR/premium_thumbnail_buttery_smooth.png"
render "TextPremiumHeroSaaSBlue"        "$BRACE_DIR/text_premium_hero_saas_blue.png"

# ── PR 6 — authoring-DSL migration validator ───────────────────────────────
render "PremiumThumbnailSaaSBlueAuthored" "$AUTHORED_DIR/premium_thumbnail_saas_blue.png"

# ── Byte-equivalence assertion ─────────────────────────────────────────────
if command -v python3 >/dev/null 2>&1; then
  echo ""
  echo "▸ Running tools/compare_pngs.py $BRACE_DIR $AUTHORED_DIR"
  if python3 tools/compare_pngs.py "$BRACE_DIR" "$AUTHORED_DIR" | tee output/diff_report.txt; then
    echo "✅ Byte-equivalence: PremiumThumbnailSaaSBlue == PremiumThumbnailSaaSBlueAuthored"
  else
    rc=$?
    echo ""
    echo "❌ Byte-equivalence DIFF detected (see output/diff_report.txt)."
    echo "  This is expected if the authoring surface evolved past the source;"
    echo "  re-validate the surface boundary in the migration doc."
    exit 2
  fi
else
  echo ""
  echo "▸ Skipping compare_pngs.py (python3 not found); renders only."
fi

echo ""
echo "Done."
echo "Telemetry DB: $HOME/.chronon3d/telemetry/chronon3d_render_history.sqlite"
echo "Open the dashboard after refresh:"
echo "  http://51.91.11.36:5173/"
