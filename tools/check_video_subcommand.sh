#!/usr/bin/env bash
# =============================================================================
# tools/check_video_subcommand.sh
#
# Cat-1 build verification helper for the `chronon3d_cli video` subcommand.
#
# Background
# ----------
# The default CMake preset `linux-fast-dev` (used by build-fast.sh) sets
#   cmake/presets/linux-fast-dev.json :: cacheVariables ::
#     "CHRONON3D_ENABLE_VIDEO": "OFF"
# to keep inner-loop dev cycles fast (video + ffmpeg + x264 chain skipped).
#
# However, several pieces of the codebase (and docs/TELEMETRY_DASHBOARD.md §2)
# reference `chronon3d_cli video <comp>` as the canonical way to produce MP4
# with auto-telemetry.  When developers run that on a fast-dev build they
# hit "A subcommand is required" because the chrono3d_cli_video static
# library was never compiled and the `register_video_commands` symbol is
# dead-coded out of the binary.
#
# This script makes that gap visible: it reports the presence/absence of the
# `video` subcommand in the local chronon3d_cli binary and prints the exact
# one-liner override to enable it without touching the committed preset.
#
# Usage
# -----
#   bash tools/check_video_subcommand.sh
#   bash tools/check_video_subcommand.sh /path/to/chronon3d_cli   # override path
#
# Exit codes
# ----------
#   0  video subcommand wired (CHRONON3D_ENABLE_VIDEO=ON in this build)
#   1  video subcommand missing in the supplied binary (pre-fix state)
#   2  chronon3d_cli binary not found / not executable (precondition fail)
#
# Reference
# ---------
# - docs/TELEMETRY_DASHBOARD.md §2 (CLI commands for video)
# - cmake/presets/linux-fast-dev.json :: cacheVariables.CHRONON3D_ENABLE_VIDEO
# - apps/chronon3d_cli/commands/video/register_video_commands.cpp (source
#   registration; requires chrono3d_cli_video static lib at link time)
# - TICKET-style fix in tools/wrap_push.sh / AGENTS freeze (Cat-1 OK)
# =============================================================================

set +e

# ── Resolve CLI binary path ──────────────────────────────────────────────────
DEFAULT_BIN="build/chronon/linux-fast-dev/apps/chronon3d_cli/chronon3d_cli"
if command -v chronon3d_cli >/dev/null 2>&1; then
    BIN="${1:-$(command -v chronon3d_cli)}"
else
    BIN="${1:-$DEFAULT_BIN}"
fi

# Allow override via env var (handy for CI runners that build to a custom dir)
[ -n "$CHRONON3D_CLI_PATH" ] && BIN="$CHRONON3D_CLI_PATH"

# ── Precondition: binary present and executable ─────────────────────────────
if [ ! -e "$BIN" ]; then
    echo "✗ chronon3d_cli not found at: $BIN"
    echo "  Build it first: bash tools/chronon-linux.sh"
    exit 2
fi
if [ ! -x "$BIN" ]; then
    echo "✗ chronon3d_cli exists but is not executable: $BIN"
    echo "  chmod +x, or rebuild."
    exit 2
fi

# ── Probe subcommand presence via clap's "--help" output ────────────────────
HELP_OUT="$("$BIN" --help 2>&1)"
if echo "$HELP_OUT" | grep -qE '^[[:space:]]*video[[:space:]]*(:|$)'; then
    echo "✓ video subcommand wired  ($BIN)"
    echo "$HELP_OUT" | grep -E '^[[:space:]]*video[[:space:]]*(:|$)' | head -1
    echo
    echo "Smoke test (no rendering):"
    "$BIN" video --help 2>&1 | head -3
    exit 0
fi

# ── Missing: print actionable override recipe with build cache context ───────
echo "✗ video subcommand MISSING from: $BIN"
echo
echo "── Diagnose ──"
CMAKE_CACHE="$(dirname "$BIN")/../../../../CMakeCache.txt"
if [ -f "$CMAKE_CACHE" ]; then
    grep -E 'CHRONON3D_ENABLE_VIDEO:BOOL' "$CMAKE_CACHE" | head -1 \
        || echo "  (CHRONON3D_ENABLE_VIDEO not in CMakeCache.txt — sourcing from CMakeLists.txt default)"
else
    echo "  no CMakeCache.txt found in the standard location for this binary."
fi
echo
echo "── Fix (non-preset override: keeps fast-dev's other speed wins) ──"
cat <<'OVERRIDE'
  cmake -S . -B build/chronon/linux-fast-dev -DCHRONON3D_ENABLE_VIDEO=ON
  cmake --build build/chronon/linux-fast-dev --target chronon3d_cli -- -j$(nproc)

  # Then re-run this check:
  bash tools/check_video_subcommand.sh
  # Expected: "✓ video subcommand wired (...)"
OVERRIDE
echo
echo "Alternative: use a video-capable preset (slower full build) ──"
cat <<'ALT'
  cmake --preset release   # or 'development' or 'profiling' (see cmake/presets/)
  cmake --build --preset release --target chronon3d_cli -- -j$(nproc)
  bash tools/check_video_subcommand.sh build/release/apps/chronon3d_cli/chronon3d_cli
ALT
exit 1
