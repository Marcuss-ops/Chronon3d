#!/usr/bin/env bash
# tools/run_pilot.sh
# ─────────────────────────────────────────────────────────────────────
# 7-day pilot driver — orchestrates render + metric logging for the
# Configurable pilot config (default: configs/pilot.instagram.yaml).
#
# Created 2026-07-12 — Test 16 / First-Principles Product Check #16.
#
# Honest framing (per AGENTS.md §honesty "non inventare"):
#   - This script is the FRAMEWORK entry point. It does NOT auto-post.
#   - It renders a video via `chronon3d_cli video`, validates the output,
#     and appends the metric row to the per-pilot JSONL log.
#   - Manual post to Instagram (or any future channel) is the runner's job
#     and is recorded via `bash tools/run_pilot.sh log ...` subcommand.
#
# Usage:
#   bash tools/run_pilot.sh --config configs/pilot.instagram.yaml render --slug my-shot-01
#   bash tools/run_pilot.sh --config configs/pilot.instagram.yaml log    my-shot-01 video_posted=true
#   bash tools/run_pilot.sh --config configs/pilot.instagram.yaml log    my-shot-01 discarded=true notes="..."`
#   bash tools/run_pilot.sh --config configs/pilot.instagram.yaml summary
#   bash tools/run_pilot.sh --config configs/pilot.instagram.yaml summary --json
#   bash tools/run_pilot.sh --help
#
# Exit codes: 0 = OK, 1 = user error, 2 = dependency missing, 3 = render failure.
# ─────────────────────────────────────────────────────────────────────
set -u

GATE_NAME=run_pilot
CONFIG_PATH="${PILOT_CONFIG:-configs/pilot.instagram.yaml}"
SUBCOMMAND=""

# ── Utilities ────────────────────────────────────────────────────────
log_info()  { printf '%s\n' "[INFO] ${GATE_NAME}: $*"; }
log_warn()  { printf '%s\n' "[WARN] ${GATE_NAME}: $*" >&2; }
log_error() { printf '%s\n' "[FAIL] ${GATE_NAME}: $*" >&2; }
die()       { log_error "$@"; exit 1; }
need()      { command -v "$1" >/dev/null 2>&1 || die "missing dependency: $1"; }

usage() {
  sed -n '2,28p' "$0" | sed 's/^# \{0,1\}//'
  cat <<USAGE
Options:
  --config <path>         Path to YAML (default: configs/pilot.instagram.yaml)
  --dry-run               Print plan without executing render or log append
  --help                  Show this help

Subcommands:
  render --slug <slug>    Render one video via chronon3d_cli + append metric row
  log <slug> key=val...   Append a metric update for an existing slug
                          (e.g. video_posted=true, discarded=true, manual_corrections=2)
  summary                 Print aggregate counters (videos_posted / discarded / total)
  summary --json          Emit machine-readable JSON
  cron-iter               Cron-friendly: render one video at a time if no draft exists
USAGE
}

# ── Argument parsing ─────────────────────────────────────────────────
DRY_RUN=false
while [ "$#" -gt 0 ]; do
  case "$1" in
    --config) CONFIG_PATH="$2"; shift 2 ;;
    --dry-run) DRY_RUN=true; shift ;;
    --help|-h) usage; exit 0 ;;
    --slug) SLUG="$2"; shift 2 ;;
    --json) EMIT_JSON=true; shift ;;
    --*) die "unknown option: $1 (use --help)" ;;
    *)
      if [ -z "$SUBCOMMAND" ]; then SUBCOMMAND="$1"; shift
      else ARGS+=("$1"); shift
      fi
      ;;
  esac
done

# ── Config validation (lightweight — no PyYAML dependency) ───────────
[ -f "$CONFIG_PATH" ] || die "config not found: $CONFIG_PATH"
need python3   || exit 2   # python3 for JSON emission
need jq        || exit 2   # jq for config parsing
need chronon3d_cli  || log_warn "chronon3d_cli not found on PATH; render subcommand will FAIL — proceeding for log/summary only"

# Extract metrics.jsonl_log from YAML (single line via grep+cut)
JSONL_LOG=$(grep -E '^\s*jsonl_log:' "$CONFIG_PATH" | head -n1 | awk '{print $2}' | tr -d '"' || true)
[ -n "$JSONL_LOG" ] || die "config lacks metrics.jsonl_log: $CONFIG_PATH"

COMPOSITION=$(grep -E '^\s*composition:' "$CONFIG_PATH" | head -n1 | awk '{print $2}' | tr -d '"' || true)
WIDTH=$(grep -E '^\s*width:' "$CONFIG_PATH" | head -n1 | awk '{print $2}' || true)
HEIGHT=$(grep -E '^\s*height:' "$CONFIG_PATH" | head -n1 | awk '{print $2}' || true)
FPS=$(grep -E '^\s*fps:' "$CONFIG_PATH" | head -n1 | awk '{print $2}' || true)
OUTPUT_DIR=$(grep -E '^\s*output_dir:' "$CONFIG_PATH" | head -n1 | awk '{print $2}' | tr -d '"' || true)

log_info "config=$CONFIG_PATH jsonl_log=$JSONL_LOG composition=$COMPOSITION dry_run=$DRY_RUN"

# ── Subcommand: render ──────────────────────────────────────────────
cmd_render() {
  [ -n "${SLUG:-}" ] || die "--slug required for render"
  need chronon3d_cli || exit 2

  OUT="$OUTPUT_DIR/${SLUG}.mp4"
  mkdir -p "$OUTPUT_DIR"

  cmd=(chronon3d_cli video "$COMPOSITION"
       --width "$WIDTH" --height "$HEIGHT" --fps "$FPS"
       --motion-blur -o "$OUT")

  if [ "$DRY_RUN" = true ]; then
    log_info "[dry-run] ${cmd[*]}"
    log_info "[dry-run] would append pilot.jsonl row for slug=$SLUG"
    return 0
  fi

  log_info "rendering: ${cmd[*]}"
  "${cmd[@]}" || { log_error "render FAILED for slug=$SLUG"; exit 3; }

  # Append seed row with render_ms placeholder (run-level detail to be filled later)
  ts=$(date -u +%Y-%m-%dT%H:%M:%SZ)
  python3 - <<PYEOF
import json, time
row = {
    "timestamp_iso": "$ts",
    "day_index": None,
    "composition_id": "$COMPOSITION",
    "output_path": "$OUT",
    "duration_s": None,
    "render_ms": None,
    "render_cost_usd": 0.0,
    "manual_corrections": 0,
    "time_saved_vs_baseline_min": None,
    "bugs": "",
    "video_posted": False,
    "discarded": False,
    "notes": "seed row from render subcommand",
    "slug": "$SLUG",
}
with open("$JSONL_LOG", "a", encoding="utf-8") as f:
    f.write(json.dumps(row, ensure_ascii=False) + "\n")
PYEOF
  log_info "render complete + pilot.jsonl appended: slug=$SLUG -> $OUT"
}

# ── Subcommand: log <slug> key=val... ───────────────────────────────
cmd_log() {
  [ "$#" -ge 2 ] || die "log requires: <slug> <key=val>..."
  _slug="$1"; shift
  _exists=$(grep -F "\"slug\": \"$_slug\"" "$JSONL_LOG" 2>/dev/null | tail -n 1 || true)
  [ -n "$_exists" ] || die "no pilot.jsonl row for slug=$_slug (run render first)"

  # Apply k/v updates to the LATEST row matching slug (in-place rewrite of JSONL)
  _tmp=$(mktemp)
  python3 - "$JSONL_LOG" "$_tmp" "$_slug" "$@" <<'PYEOF'
import json, sys
src, dst, target_slug = sys.argv[1], sys.argv[2], sys.argv[3]
kv = {}
for a in sys.argv[4:]:
    if "=" in a:
        k, v = a.split("=", 1)
        # Coerce boolean/numeric if possible
        if v in ("true", "True"):   v = True
        elif v in ("false", "False"): v = False
        else:
            try: v = int(v)
            except ValueError:
                try: v = float(v)
                except ValueError: pass
        kv[k] = v

with open(src, "r", encoding="utf-8") as fin, open(dst, "w", encoding="utf-8") as fout:
    for line in fin:
        try:
            row = json.loads(line)
        except json.JSONDecodeError:
            fout.write(line); continue
        if row.get("slug") == target_slug:
            row.update(kv)
            row["updated_at_iso"] = __import__("datetime").datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ")
        fout.write(json.dumps(row, ensure_ascii=False) + "\n")
PYEOF
  mv "$_tmp" "$JSONL_LOG"
  log_info "log updated: slug=$_slug keys=$(echo "$@" | tr ' ' ',')"
}

# ── Subcommand: summary [--json] ────────────────────────────────────
cmd_summary() {
  python3 "$0" --summary-py "$JSONL_LOG" "${EMIT_JSON:-false}" 2>/dev/null
  python3 - "$JSONL_LOG" "${EMIT_JSON:-false}" <<'PYEOF'
import json, sys
log_path, as_json = sys.argv[1], sys.argv[2] == "true"
n_total = n_posted = n_discarded = n_bugs = 0
manual_corrections = []
time_saved = []
duration_s = []
render_ms = []
try:
    with open(log_path, "r", encoding="utf-8") as f:
        for line in f:
            try: row = json.loads(line)
            except json.JSONDecodeError: continue
            n_total += 1
            if row.get("video_posted"):     n_posted += 1
            if row.get("discarded"):        n_discarded += 1
            if row.get("bugs"):             n_bugs += 1
            mc = row.get("manual_corrections")
            if isinstance(mc, (int, float)): manual_corrections.append(int(mc))
            ts = row.get("time_saved_vs_baseline_min")
            if isinstance(ts, (int, float)): time_saved.append(float(ts))
            d = row.get("duration_s")
            if isinstance(d, (int, float)):  duration_s.append(float(d))
            r = row.get("render_ms")
            if isinstance(r, (int, float)):  render_ms.append(float(r))
except FileNotFoundError:
    pass

def median(xs):
    if not xs: return None
    s = sorted(xs); n = len(s)
    return s[n//2] if n % 2 else (s[n//2 - 1] + s[n//2]) / 2

result = {
    "total_rows": n_total,
    "video_posted_count": n_posted,
    "discarded_count": n_discarded,
    "bugs_count": n_bugs,
    "manual_corrections_median": median(manual_corrections),
    "time_saved_min_median": median(time_saved),
    "video_duration_s_median": median(duration_s),
    "render_ms_median": median(render_ms),
    "video_post_rate_pct": round(100 * n_posted / n_total, 1) if n_total else 0.0,
}

if as_json:
    print(json.dumps(result, ensure_ascii=False, indent=2))
else:
    print(f"=== 7-day pilot summary ({log_path}) ===")
    for k, v in result.items():
        print(f"  {k:33s} = {v}")
PYEOF
}

# ── Dispatch ─────────────────────────────────────────────────────────
case "${SUBCOMMAND:-help}" in
  render) cmd_render ;;
  log)    shift; cmd_log "$@" ;;
  summary) cmd_summary ;;
  "") usage; exit 1 ;;
  *)     die "unknown subcommand: $SUBCOMMAND (use --help)" ;;
esac
