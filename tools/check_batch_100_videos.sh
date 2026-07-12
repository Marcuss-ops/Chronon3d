#!/usr/bin/env bash
# ============================================================================
# check_batch_100_videos.sh — gate [Test #20] for batch_100_videos
# ============================================================================
# 4-envelope PASS-criteria audit on the 100-job batch acceptance test.
# Per AGENTS.md v0.1:
#   - reads the append-only JSONL at $BATCH_100_VIDEOS_LOG (default
#     `~/.chronon3d/telemetry/batch_100_videos.jsonl`)
#   - reads the canonical config at $BATCH_100_VIDEOS_CONFIG (default
#     `configs/batch_100_videos_corpus.yaml`)
#   - emits GATE_FAIL if any of the 4 PASS-criteria envelopes is breached:
#       output_count              — completed == 100
#       zero_crashes              — crashes_total == 0
#       zero_corrupted            — corrupted_total == 0
#       at_least_98_pct_no_manual — manual_touches_total <= 2
#   - exits 2 on missing python3/pyyaml/config (fail-loud per AGENTS.md
#     §honest-limitation; NOT GATE_FAIL=1 — distinct per AGENTS.md gate
#     exit-code convention)
#
# Per AGENTS.md `## Regole di lint documentale` ### INFO-level diagnostic style:
# emits `[INFO] ${GATE_NAME}: ...` line on clean PASS as addizionale al
# canonico `GATE_PASS`; the FAIL path stays unchanged (GATE_FAIL: with the
# list of offending envelopes on stderr).
#
# Companion selftest: tests/tools/selftest_batch_100_videos.sh
# exercises 4/4 scenarios (PASS happy / FAIL_crash / FAIL_corrupt /
# FAIL_manual_3) via synthetic JSONL emission + gate invocation.
#
# Zero-data forwarding when log absent (first-install onboard is permissive
# per AGENTS.md §honesty "non inventare"); threshold envelopes apply once
# ≥1 entry lands (the gate emits GATE_PASS + 0-events INFO line).
# ============================================================================

set -uo pipefail

GATE_NAME=check_batch_100_videos

REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || echo "$(cd "$(dirname "$0")/.." && pwd)")"
CONFIG_PATH="${BATCH_100_VIDEOS_CONFIG:-$REPO_ROOT/configs/batch_100_videos_corpus.yaml}"
LOG_PATH="${BATCH_100_VIDEOS_LOG:-$HOME/.chronon3d/telemetry/batch_100_videos.jsonl}"

# ── 1. PRECOND: python3 + pyyaml (fail-loud per AGENTS.md §honest-limitation) ──
if ! command -v python3 >/dev/null 2>&1; then
    echo "GATE_FAIL: python3 missing; install python3 to evaluate 100-batch metrics" >&2
    exit 2
fi
if ! python3 -c "import yaml" >/dev/null 2>&1; then
    echo "GATE_FAIL: python3 yaml missing; install python3-yaml to parse the config" >&2
    exit 2
fi
if [ ! -f "$CONFIG_PATH" ]; then
    echo "GATE_FAIL: config missing: $CONFIG_PATH" >&2
    exit 2
fi

# ── 2. PRECOND: log absent → zero-data forwarding (permissive first-install) ──
if [ ! -f "$LOG_PATH" ]; then
    EVENT_COUNT=0
    echo "[INFO] ${GATE_NAME}: zero-data forwarding - no batch_100_videos.jsonl events found (first-install onboard is permissive per AGENTS.md §honesty)"
    echo "GATE_PASS: 0 batch_100_videos events (zero-data forwarding per AGENTS.md §honesty; thresholds apply once >=1 entry lands)"
    exit 0
fi

# ── 3. Parse log + evaluate 4 PASS criteria via Python ──────────────────
RESULT="$(BATCH_100_VIDEOS_LOG="$LOG_PATH" \
          BATCH_100_VIDEOS_CONFIG="$CONFIG_PATH" \
          python3 - <<'PY'
import json, os, sys
try:
    import yaml
except Exception:
    print("FAIL_PRECOND: pyyaml missing")
    sys.exit(2)

log_path = os.environ.get("BATCH_100_VIDEOS_LOG", "")
config_path = os.environ.get("BATCH_100_VIDEOS_CONFIG", "")

events = []
with open(log_path, "r") as f:
    for ln in f:
        ln = ln.strip()
        if not ln:
            continue
        try:
            events.append(json.loads(ln))
        except Exception:
            pass

summary = {
    "jobs_total": len(events),
    "completed": sum(int(e.get("completed", 0)) for e in events),
    "failed": sum(int(e.get("failed", 0)) for e in events),
    "manual_touches_total": sum(int(e.get("manual_touches", 0)) for e in events),
    "total_time_ms_total": sum(int(e.get("total_time_ms", 0)) for e in events),
    "peak_RAM_MB_max": max((int(e.get("peak_RAM_MB", 0)) for e in events), default=0),
    "crashes_total": sum(int(e.get("crashes", 0)) for e in events),
    "corrupted_total": sum(int(e.get("corrupted", 0)) for e in events),
    "invisible_text_total": sum(int(e.get("invisible_text", 0)) for e in events),
}

# Thresholds per user spec + YAML pass_criteria schema:
#   - 100 output     (completed == 100)
#   - 0 crash        (crashes_total == 0)
#   - 0 corrotti     (corrupted_total == 0)
#   - >=98% no manual (manual_touches_total <= 2 → 100-2=98 of 100)
verdict = {
    "output_count":              "PASS" if summary["completed"] == 100 else "FAIL",
    "zero_crashes":              "PASS" if summary["crashes_total"] == 0 else "FAIL",
    "zero_corrupted":            "PASS" if summary["corrupted_total"] == 0 else "FAIL",
    "at_least_98_pct_no_manual": "PASS" if summary["manual_touches_total"] <= 2 else "FAIL",
}
overall = "PASS" if all(v == "PASS" for v in verdict.values()) else "FAIL"
print("SUMMARY_JSON=" + json.dumps({"summary": summary, "verdicts": verdict, "overall": overall}))
PY
)"
RC=$?
if [ "$RC" -ne 0 ]; then
    echo "GATE_FAIL: python evaluator failed (exit $RC)" >&2
    echo "$RESULT" >&2
    exit 2
fi

SUMMARY_LINE="$(echo "$RESULT" | grep '^SUMMARY_JSON=' | head -1)"
if [ -z "$SUMMARY_LINE" ]; then
    echo "GATE_FAIL: python evaluator produced no SUMMARY_JSON line" >&2
    exit 2
fi
SUMMARY_JSON="${SUMMARY_LINE#SUMMARY_JSON=}"

# Helper: extract JSON value via python -c (single-line, simple type coercion).
json_get() {
    local key="$1"
    python3 -c "import json,sys; v=json.loads(sys.argv[1])['$key']; print(v)" "$SUMMARY_JSON"
}

EVENTS_TOTAL="$(json_get summary.jobs_total)"
COMPLETED="$(json_get summary.completed)"
MANUAL_TOTAL="$(json_get summary.manual_touches_total)"
CRASHES_TOTAL="$(json_get summary.crashes_total)"
CORRUPTED_TOTAL="$(json_get summary.corrupted_total)"
OVERALL_VERDICT="$(json_get overall)"
OC_VERDICT="$(json_get verdicts.output_count)"
ZC_VERDICT="$(json_get verdicts.zero_crashes)"
ZO_VERDICT="$(json_get verdicts.zero_corrupted)"
NM_VERDICT="$(json_get verdicts.at_least_98_pct_no_manual)"

# ── 4. Emit verdict (PASS canonical + [INFO] addizionale OR GATE_FAIL: list) ────
if [ "$OVERALL_VERDICT" = "PASS" ]; then
    echo "GATE_PASS: all 4 PASS criteria met for batch_100_videos ($EVENTS_TOTAL jobs / $COMPLETED completed / $MANUAL_TOTAL manual / $CRASHES_TOTAL crashes / $CORRUPTED_TOTAL corrupt)"
    echo "[INFO] ${GATE_NAME}: $EVENTS_TOTAL events / $COMPLETED completed / $MANUAL_TOTAL manual / $CRASHES_TOTAL crashes / $CORRUPTED_TOTAL corrupt (overall=PASS, envelopes output_count=$OC_VERDICT, zero_crashes=$ZC_VERDICT, zero_corrupted=$ZO_VERDICT, at_least_98_pct_no_manual=$NM_VERDICT)"
    exit 0
else
    echo "GATE_FAIL: at least one PASS-criterion envelope breached on batch_100_videos:" >&2
    echo "  output_count              = $OC_VERDICT (completed=$COMPLETED, expected 100)" >&2
    echo "  zero_crashes              = $ZC_VERDICT (crashes=$CRASHES_TOTAL, expected 0)" >&2
    echo "  zero_corrupted            = $ZO_VERDICT (corrupted=$CORRUPTED_TOTAL, expected 0)" >&2
    echo "  at_least_98_pct_no_manual = $NM_VERDICT (manual_touches_total=$MANUAL_TOTAL, expected <=2)" >&2
    exit 1
fi
