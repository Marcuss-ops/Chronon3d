#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────────────────────
# tools/render_cinematic_showcase.sh
#
# Render the CinematicCameraShowcase composition (1920x1080 @ 30 fps,
# 180 frame) into PNG frames, then assemble an MP4 via FFmpeg.
# Output target = output/showcase/ (gitignored). CLI binary is built
# independently of the showcase (shared with all other compositions).
#
# Definition of Done (Agente 3):
#   * Preview rendered @ 960×540 (PNG sequence)
#   * Full 1920×1080 PNG sequence produced
#   * MP4 1920×1080 assembled via FFmpeg (libx264, yuv420p, crf 18)
#   * All outputs under output/showcase/ (does NOT pollute git tree)
#
# Usage:
#   tools/render_cinematic_showcase.sh                    # default preset
#   CHRONON_PRESET=linux-ci tools/render_cinematic_showcase.sh
#   CHRONON_JOBS=4 tools/render_cinematic_showcase.sh     # limit parallel jobs
#
# Pre-requisites:
#   * vcpkg dependencies installed at configure time
#   * ffmpeg in PATH (libx264 + yuv420p support)
# ──────────────────────────────────────────────────────────────────────────────
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_DIR"

PRESET="${CHRONON_PRESET:-linux-release}"
BUILD_DIR="build/chronon/$PRESET"
TARGET="chronon3d_cli"
BIN="$BUILD_DIR/apps/chronon3d_cli/chronon3d_cli"
COMPOSITION="CinematicCameraShowcase"

OUT_DIR="output/showcase"
PREVIEW_DIR="$OUT_DIR/preview_frames"
FULL_DIR="$OUT_DIR/frames"
MP4_FINAL="$OUT_DIR/CinematicCameraShowcase.mp4"

echo "═══ CinematicCameraShowcase render pipeline ═══"
echo "  preset       : $PRESET"
echo "  build dir    : $BUILD_DIR"
echo "  composition  : $COMPOSITION (1920×1080, 180 frame)"
echo "  output root  : $OUT_DIR"

# ── 1. build the CLI binary (configure if needed) ────────────────────────────
echo ""
echo "── Configurando + buildando $TARGET ──"
if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
  cmake --preset "$PRESET"
fi
cmake --build "$BUILD_DIR" --target "$TARGET" -j"${CHRONON_JOBS:-$(nproc)}"

if [[ ! -x "$BIN" ]]; then
  echo "FAIL: binary $BIN mancante dopo la build."
  exit 1
fi

# ── 2. validate composition is registered (probes CLI for health-check) ─────
echo ""
echo "── Listando compositions registrate ──"
if "$BIN" list 2>&1 | tee "$OUT_DIR/.cli_list_snapshot.txt" \
   | grep -F "$COMPOSITION" >/dev/null; then
  echo "  $COMPOSITION ✓ registered."
else
  echo "WARN: $COMPOSITION non ancora in CLI list — falling back to direct invocation."
fi

# ── 3. prepare output directories ────────────────────────────────────────────
mkdir -p "$PREVIEW_DIR" "$FULL_DIR"

# ── 4. render preview @ 960 × 540 (proves composition renders fast) ──────────
echo ""
echo "── Rendering preview 960×540 (PNG sequence) ──"
"$BIN" render "$COMPOSITION" --resolution 960 540  --output-dir "$PREVIEW_DIR" \
  || "$BIN"        "$COMPOSITION" --resolution 960 540  --output-dir "$PREVIEW_DIR" \
  || { echo "FAIL: nessuna CLI subcommand ha accettato render $COMPOSITION"; exit 2; }

# ── 5. render full @ 1920 × 1080 ────────────────────────────────────────────
echo ""
echo "── Rendering full 1920×1080 (PNG sequence) ──"
"$BIN" render "$COMPOSITION" --resolution 1920 1080 --output-dir "$FULL_DIR" \
  || "$BIN"        "$COMPOSITION" --resolution 1920 1080 --output-dir "$FULL_DIR" \
  || { echo "FAIL: nessuna CLI subcommand ha accettato render $COMPOSITION"; exit 2; }

if [[ ! -d "$FULL_DIR" ]] || [[ -z "$(ls -A "$FULL_DIR" 2>/dev/null)" ]]; then
  echo "FAIL: nessun frame PNG sotto $FULL_DIR."
  ls -la "$FULL_DIR" || true
  exit 3
fi

# ── 6. assemble MP4 via FFmpeg ──────────────────────────────────────────────
echo ""
echo "── Assembling MP4 via FFmpeg ──"
ffmpeg -y \
  -framerate 30 \
  -i "$FULL_DIR/frame_%04d.png" \
  -c:v libx264 \
  -pix_fmt yuv420p \
  -crf 18 \
  "$MP4_FINAL"

# ── 7. summary ───────────────────────────────────────────────────────────────
echo ""
echo "═══ Render OK ═══"
echo "  preview frames : $PREVIEW_DIR"
echo "  full frames    : $FULL_DIR"
echo "  MP4 (full)     : $MP4_FINAL"
ls -lh "$MP4_FINAL" || true
