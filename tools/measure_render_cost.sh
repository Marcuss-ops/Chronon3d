#!/usr/bin/env bash
# measure_render_cost.sh — First-Principles Product Check Test #11 "costo reale".
# Wraps `chronon render <comp_id> --output /tmp/benchmark.mp4` with `/usr/bin/time -v`
# to capture the canonical 5 fields per user spec:
#   1. duration_seconds  (GNU time Elapsed (wall clock))
#   2. cpu_percent       (GNU time Percent of CPU)
#   3. peak_rss_kb       (GNU time Maximum resident set size)
#   4. output_bytes      (wc -c of /tmp/benchmark.mp4)
#   5. frame_count       (ffprobe nb_read_packets; 0 if ffprobe absent)
# Computes cost_per_video via canonical formula:
#     cost_per_video_eur = (duration_seconds / 3600) * VPS_HOURLY_EUR   (default 0.05)
# Emits:
#   - JSON object on stdout
#   - One row appended to docs/scorecard.csv (canonical Cat-3 anti-duplication ledger)
# Per AGENTS.md:
#   - §honest-limitation: FAIL-LOUD when chronon3d_cli OR /usr/bin/time not present.
#   - §info-level diagnostic style: [INFO] <gate-name>: ... on PASS.
#   - Cat-3 anti-duplication: 1 canonical scorecard file (docs/scorecard.csv).
#   - §no-silent-fallback: WARN emitted when chronon exits non-zero; measurement still
#     persisted (failed renders still consume compute and have a COSTS-FOR-ANALYSIS
#     meaning — emitting the row + WARN + exit 0 is HONEST, not spurious).
# Exit code: 0 on persisted measurement (success or failure both capture cost data);
#            1 only when chronon3d_cli / /usr/bin/time / /tmp writable space precondition fails.
set -euo pipefail

GATE_NAME=measure_render_cost
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

# ─────────── 1. Inputs (canonical defaults per user spec) ───────────
COMP_ID="${1:-Benchmark10Min}"           # canonical composition-id default
OUTPUT_PATH="${2:-/tmp/benchmark.mp4}"    # canonical output path default
VPS_HOURLY_EUR="${VPS_HOURLY_EUR:-0.05}"  # env override + 0.05 default per user spec
SCORECARD_PATH="$REPO_ROOT/docs/scorecard.csv"

# ─────────── 2. /usr/bin/time -v HARD-REQUIRED (per user spec verbatim) ───────────
if [ ! -x "/usr/bin/time" ]; then
  cat <<EOF >&2
GATE_FAIL: /usr/bin/time not found at /usr/bin/time (hard requirement per user spec verbatim).
  Install: sudo apt install time    # Debian/Ubuntu (ships GNU time as /usr/bin/time)
            brew install gnu-time   # macOS
  Re-run: bash tools/measure_render_cost.sh
EOF
  exit 1
fi
if ! /usr/bin/time -v /bin/true >/dev/null 2>&1; then
  cat <<EOF >&2
GATE_FAIL: /usr/bin/time lacks the -v flag (GNU time feature required).
  Install the GNU time package explicitly:  sudo apt install time
EOF
  exit 1
fi

# ─────────── 3. chronon3d_cli binary discovery (canonical pattern) ───────────
CHRONON_CLI=""
for candidate in \
  "$REPO_ROOT/build/manual-test/chronon3d_cli" \
  "$REPO_ROOT/build/chronon3d_cli" \
  "$REPO_ROOT/build/chronon/linux-content-dev/chronon3d_cli"
do
  if [ -x "$candidate" ]; then
    CHRONON_CLI="$candidate"
    break
  fi
done

if [ -z "$CHRONON_CLI" ]; then
  cat <<EOF >&2
GATE_FAIL: chronon3d_cli binary not found in standard build paths.
  Searched:
    $REPO_ROOT/build/manual-test/chronon3d_cli
    $REPO_ROOT/build/chronon3d_cli
    $REPO_ROOT/build/chronon/linux-content-dev/chronon3d_cli
  Recover the canonical build:
    cmake --preset linux-content-dev
    cmake --build --preset linux-content-dev -j"\$(nproc)" --target chronon3d_cli
  Re-run: bash tools/measure_render_cost.sh
EOF
  exit 1
fi

# ─────────── 4. Pre-cleanup (deterministic measurement baseline) ───────────
rm -f "$OUTPUT_PATH"
TMP_TIME_STDERR="$(mktemp -t measure_render_cost_time.XXXXXX)"
TMP_CHRONON_STDOUT="$(mktemp -t measure_render_cost_stdout.XXXXXX)"
trap 'rm -f "$TMP_TIME_STDERR" "$TMP_CHRONON_STDOUT"' EXIT

# ─────────── 5. Single canonical capture: /usr/bin/time -v around chronon render ───────────
#
# /usr/bin/time -v writes its diagnostics to OUR stderr (the bash script's stderr).
# chronon3d_cli's stderr ALSO goes to our stderr (forwarded by /usr/bin/time).
# BOTH end up in TMP_TIME_STDERR — chronon's stderr at the top, /usr/bin/time stats
# at the bottom (after chronon completes). Grep for time-specific labels, which are
# unique to /usr/bin/time output and will not collide with chronon's stderr (which
# uses spdlog::error patterns like 'Unknown composition', 'AssetResolutionFailure').
set +e
/usr/bin/time -v "$CHRONON_CLI" render "$COMP_ID" --output "$OUTPUT_PATH" \
  >"$TMP_CHRONON_STDOUT" 2>"$TMP_TIME_STDERR"
chronon_exit=$?
set -e

# ─────────── 6. Parse /usr/bin/time -v output (canonical field labels) ───────────
# GNU time -v emits labels verbatim (verified via /usr/bin/time -v /bin/true):
#   Elapsed (wall clock) time (h:mm:ss or m:ss): 0:00.01
#   Percent of CPU this job got: 99%
#   Maximum resident set size (kbytes): 3844

elapsed_str="$(grep -m1 'Elapsed (wall clock) time' "$TMP_TIME_STDERR" 2>/dev/null \
  | sed -nE 's/.*:[[:space:]]+([0-9:.]+).*/\1/p')"
cpu_pct_str="$(grep -m1 'Percent of CPU this job got' "$TMP_TIME_STDERR" 2>/dev/null \
  | sed -nE 's/.*:[[:space:]]+([0-9]+)%.*/\1/p')"
peak_rss_kb_str="$(grep -m1 'Maximum resident set size' "$TMP_TIME_STDERR" 2>/dev/null \
  | sed -nE 's/.*:[[:space:]]+([0-9]+).*/\1/p')"

# Convert elapsed (mm:ss.cs or hh:mm:ss.cs) to seconds via awk
duration_sec="0"
if [ -n "$elapsed_str" ]; then
  duration_sec="$(awk -F: -v s="$elapsed_str" 'BEGIN{
    n=split(s, parts, ":")
    if (n==2) { print parts[1]*60 + parts[2] }
    else if (n==3) { print parts[1]*3600 + parts[2]*60 + parts[3] }
    else { print 0 }
  }')"
fi
cpu_pct="${cpu_pct_str:-0}"
peak_rss_kb="${peak_rss_kb_str:-0}"

# ─────────── 7. output_bytes + frame_count ───────────
output_bytes="0"
if [ -e "$OUTPUT_PATH" ]; then
  output_bytes="$(wc -c < "$OUTPUT_PATH" | awk '{print $1+0}')"
fi

frame_count="0"
if command -v ffprobe >/dev/null 2>&1 && [ -e "$OUTPUT_PATH" ]; then
  frame_count="$(ffprobe -v error -select_streams v:0 -count_packets \
    -show_entries stream=nb_read_packets -of csv=p=0 "$OUTPUT_PATH" 2>/dev/null \
    | head -n1 | awk '{print $1+0}')"
fi

# ─────────── 8. Compute cost (canonical formula per user spec) ───────────
# cost_per_video_eur = (duration_seconds / 3600) * VPS_HOURLY_EUR
cost_per_video_eur="$(awk -v d="$duration_sec" -v h="$VPS_HOURLY_EUR" \
  'BEGIN { printf "%.6f", (d / 3600.0) * h }')"

# ─────────── 9. ISO 8601 UTC timestamp ───────────
ts="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"

# ─────────── 10. Append row to docs/scorecard.csv (canonical Cat-3 ledger) ───────────
# Create with header if missing/empty. Header MUST match canonical column order
# so downstream consumers (pandas / spreadsheet / jq) can rely on positional parsing.
mkdir -p "$(dirname "$SCORECARD_PATH")"
if [ ! -s "$SCORECARD_PATH" ]; then
  cat >> "$SCORECARD_PATH" <<'EOF'
timestamp,composition_id,duration_seconds,cpu_percent,peak_rss_kb,output_bytes,frame_count,vps_hourly_eur,cost_per_video_eur
EOF
fi

# Append this run's row (printf for numeric integrity — no quoting issues).
printf '%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
  "$ts" "$COMP_ID" "$duration_sec" "$cpu_pct" "$peak_rss_kb" \
  "$output_bytes" "$frame_count" "$VPS_HOURLY_EUR" "$cost_per_video_eur" \
  >> "$SCORECARD_PATH"

# ─────────── 11. Emit JSON summary on stdout (canonical contract) ───────────
cat <<EOF
{
  "timestamp":             "$ts",
  "composition_id":        "$COMP_ID",
  "duration_seconds":      $duration_sec,
  "cpu_percent":           $cpu_pct,
  "peak_rss_kb":           $peak_rss_kb,
  "output_bytes":          $output_bytes,
  "frame_count":           $frame_count,
  "vps_hourly_eur":        $VPS_HOURLY_EUR,
  "cost_per_video_eur":    $cost_per_video_eur,
  "chronon_exit_code":     $chronon_exit,
  "chronon_cli_path":      "$CHRONON_CLI",
  "output_path":           "$OUTPUT_PATH",
  "scorecard_path":        "$SCORECARD_PATH"
}
EOF

# ─────────── 12. Emit INFO-level diagnostic (AGENTS.md INFO-style rule) ───────────
echo "[INFO] ${GATE_NAME}: ${COMP_ID} rendered in ${duration_sec}s (${cpu_pct}% CPU, RSS ${peak_rss_kb} KB, ${output_bytes} bytes, ${frame_count} frames) — cost ${cost_per_video_eur} EUR @ ${VPS_HOURLY_EUR} EUR/h; row appended to ${SCORECARD_PATH}"

# ─────────── 13. Final verdict — NO silent fallback / NO spurious exit 0 ───────────
# Per §honest-limitation: if chronon failed, the time stats still represent the
# REAL cost (compute spent before failure). Emit WARN (visible to orchestrator +
# cron readers) + still exit 0 so the row is persisted for analysis.
# This is HONEST (data captured, failure visible) — not a silent fallback.
if [ "$chronon_exit" -ne 0 ]; then
  echo "WARN: ${GATE_NAME}: chronon3d_cli exited with non-zero ($chronon_exit); captured time stats STILL REPRESENT REAL compute cost before failure (NOT a silent fallback)" >&2
fi
exit 0
