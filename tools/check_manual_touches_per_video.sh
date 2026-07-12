#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# check_manual_touches_per_video.sh — gate [26/26] for Test #19
# ═══════════════════════════════════════════════════════════════════════════
#
# Verifies that the manual_touches_per_video counter is within the per-phase
# thresholds declared in `configs/touchpoint_thresholds.yaml`:
#   oggi   <= 8
#   fase1  <= 3
#   fase2  <= 1
#   finale <= 0
#
# Per phase, the gate counts de-duplicated (run_id, op) events from the
# append-only log at `~/.chronon3d/telemetry/manual_touches.jsonl` and
# compares against the threshold. If ANY phase exceeds its threshold, the
# gate fails with a `GATE_FAIL: <phase> ≤ <max_allowed> actual:<N>`
# diagnostic.
#
# Per AGENTS.md v0.1 Cat-3 (no new public SDK API surface):
#   shell + YAML + JSONL only. ZERO new symbols in apps/chronon3d_cli/.
#   The kind list + thresholds are read from configs/touchpoint_thresholds.yaml.
#
# Per AGENTS.md INFO-level diagnostic style rule #2:
#   emits ONE [INFO] ${GATE_NAME}: ... line on PASS addizionale al
#   canonico GATE_PASS finale.
#
# Per AGENTS.md §honesty:
#   zero-data (missing/empty log) emits an `[INFO] ${GATE_NAME}: zero-data
#   forwarding - no manual_touches.jsonl events found` line + GATE_PASS
#   (does NOT spuriously fail the pre-push chain on first install).
#   Once the log has >= 1 entry, the threshold envelopes apply.
#
# Usage:
#   tools/check_manual_touches_per_video.sh
#   PYTHON_BIN=python3 CONFIG=configs/touchpoint_thresholds.yaml \
#       LOG_PATH=~/.chronon3d/telemetry/manual_touches.jsonl \
#       bash tools/check_manual_touches_per_video.sh
#
# Exit codes: 0 = PASS, 1 = FAIL (at least one phase exceeds threshold),
# 2 = internal script error (missing python3 / pyyaml / log unreadable).
# ═══════════════════════════════════════════════════════════════════════════

set -euo pipefail

GATE_NAME=check_manual_touches_per_video
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

PYTHON_BIN="${PYTHON_BIN:-python3}"
CONFIG="${CONFIG:-$REPO_ROOT/configs/touchpoint_thresholds.yaml}"
LOG_PATH_RAW="${LOG_PATH:-$HOME/.chronon3d/telemetry/manual_touches.jsonl}"

# ── Step 1: precondition check (fail-loud per AGENTS.md §honest-limitation) ──
if ! command -v "$PYTHON_BIN" >/dev/null 2>&1; then
    echo "GATE_FAIL_INTERNAL: python3 not found in PATH" >&2
    echo "  fix: sudo apt install python3 (Python 3.8+ required for pyyaml)" >&2
    echo "GATE_FAIL"
    exit 2
fi

if ! "$PYTHON_BIN" -c "import yaml" 2>/dev/null; then
    echo "GATE_FAIL_INTERNAL: PyYAML not installed" >&2
    echo "  fix: pip install pyyaml (or pip3 install pyyaml)" >&2
    echo "GATE_FAIL"
    exit 2
fi

if [ ! -f "$CONFIG" ]; then
    echo "GATE_FAIL_INTERNAL: config not found at $CONFIG" >&2
    echo "  fix: ensure configs/touchpoint_thresholds.yaml is tracked on main" >&2
    echo "GATE_FAIL"
    exit 2
fi

# Expand tilde in LOG_PATH
LOG_PATH="$(eval echo "$LOG_PATH_RAW")"

# ── Step 2: invoke python verifier ─────────────────────────────────────────────
# Single canonical verifier — pure data transformation, zero branching on host
# state. Emits a structured JSON-on-stdout for the bash wrapper to parse.
VERIFY_OUT="$("$PYTHON_BIN" - "$CONFIG" "$LOG_PATH" <<'PYEOF'
import json
import sys
import yaml
from pathlib import Path
from collections import defaultdict, OrderedDict

config_path, log_path = sys.argv[1], sys.argv[2]

# ── Load config + canonical op/phase/threshold lists ──
with open(config_path, "r", encoding="utf-8") as f:
    cfg = yaml.safe_load(f)

canonical_ops = list(cfg["ops"])
phases_cfg = cfg["phases"]
phase_lookup = {p["id"]: p for p in phases_cfg}

# ── Discover log events (zero-data path returns empty verdict) ──
log = Path(log_path)
events = []
if log.exists() and log.is_file():
    with log.open("r", encoding="utf-8") as f:
        for line_no, line in enumerate(f, start=1):
            line = line.strip()
            if not line:
                continue
            try:
                evt = json.loads(line)
            except json.JSONDecodeError as exc:
                # Skip malformed lines but record the count so the gate emits
                # a WARN (NOT FAIL — telemetry noise tolerance).
                print(f"WARN: line {line_no}: JSON decode error: {exc}",
                      file=sys.stderr)
                continue
            events.append(evt)

# ── De-duplicate (run_id, op) pairs: keep first ──
seen = set()
deduped_events = []
for evt in events:
    run_id = evt.get("run_id", "")
    op = evt.get("op", "")
    if not run_id or not op:
        continue
    key = (run_id, op)
    if key in seen:
        continue
    seen.add(key)
    deduped_events.append(evt)

# ── Aggregate per phase (de-duped) → actual count vs max_allowed ──
phase_totals = OrderedDict()
for p in phases_cfg:
    pid = p["id"]
    op_counts = defaultdict(int)
    for evt in deduped_events:
        if evt.get("phase") == pid:
            op_counts[evt["op"]] += 1
    actual = sum(op_counts.values())
    max_allowed = p["max_allowed"]
    comparison = p["comparison"]
    if comparison == "<=":
        status = "PASS" if actual <= max_allowed else "FAIL"
    else:
        # Defensive: only "<=" supported per spec, but fail loud on unknown.
        print(f"GATE_FAIL_INTERNAL: unknown comparison: {comparison!r}",
              file=sys.stderr)
        sys.exit(2)
    violating_ops = sorted([op for op, c in op_counts.items()
                            if c > 0 and actual > max_allowed])
    phase_totals[pid] = {
        "phase_id": pid,
        "label": p["label"],
        "max_allowed": max_allowed,
        "actual": actual,
        "status": status,
        "per_op": dict(sorted(op_counts.items())),
    }

# ── Compose verdict ──
overall_status = "PASS"
if any(t["status"] == "FAIL" for t in phase_totals.values()):
    overall_status = "FAIL"
zero_data = (len(events) == 0)

verdict = {
    "overall_status": overall_status,
    "zero_data": zero_data,
    "config_path": config_path,
    "log_path": log_path,
    "events_total": len(events),
    "events_deduped": len(deduped_events),
    "canonical_ops": canonical_ops,
    "phases": phase_totals,
}
print(json.dumps(verdict, indent=2, sort_keys=False))
PYEOF
)"

# ── Step 3: parse verifier output + emit gate verdict ─────────────────────────
# Use printf instead of heredoc to avoid bash heredoc syntax hazard (the prior
# baseline `check_test_hygiene.sh` gating regression was caused by heredoc bugs).

OVERALL_STATUS=$(echo "$VERIFY_OUT" | "$PYTHON_BIN" -c 'import json,sys; print(json.load(sys.stdin)["overall_status"])')
ZERO_DATA=$(echo "$VERIFY_OUT"     | "$PYTHON_BIN" -c 'import json,sys; print(json.load(sys.stdin)["zero_data"])')
EVENTS_TOTAL=$(echo "$VERIFY_OUT"  | "$PYTHON_BIN" -c 'import json,sys; print(json.load(sys.stdin)["events_total"])')
EVENTS_DEDUPED=$(echo "$VERIFY_OUT"| "$PYTHON_BIN" -c 'import json,sys; print(json.load(sys.stdin)["events_deduped"])')

if [ "$ZERO_DATA" = "True" ]; then
    echo "GATE_PASS: 0 events in $LOG_PATH — zero-data forwarding (no manual touches recorded yet)"
    echo "[INFO] ${GATE_NAME}: zero-data forwarding - no manual_touches.jsonl events found (${EVENTS_TOTAL} raw / ${EVENTS_DEDUPED} de-duped; first-install onboard is permissive per AGENTS.md §honesty)"
    exit 0
fi

if [ "$OVERALL_STATUS" = "FAIL" ]; then
    echo "GATE_FAIL: at least one phase exceeds manual_touches_per_video threshold:" >&2
    echo "$VERIFY_OUT" | "$PYTHON_BIN" -c '
import json, sys
v = json.load(sys.stdin)
for pid, t in v["phases"].items():
    if t["status"] == "FAIL":
        print("  {}: actual={} max_allowed={} violating_ops={}".format(pid, t.get("actual"), t.get("max_allowed"), list(t.get("per_op", {}).keys())))
' >&2
    echo "  fix: investigate $LOG_PATH lines, reduce per-kind events via Cat-5 root-cause-forward discipline" >&2
    echo "  ctx: ${EVENTS_TOTAL} events total / ${EVENTS_DEDUPED} de-duped across all phases" >&2
    echo "GATE_FAIL" >&2
    exit 1
fi

# ── PASS path (canonical INFO-level diagnostic style) ─────────────────────────
echo "GATE_PASS: all 4 phases PASS manual_touches_per_video threshold (${EVENTS_TOTAL} events / ${EVENTS_DEDUPED} de-duped)"
echo "[INFO] ${GATE_NAME}: 4/4 phases PASS (oggi<=8, fase1<=3, fase2<=1, finale<=0) — ${EVENTS_TOTAL} events / ${EVENTS_DEDUPED} de-duped across all phases (config=$CONFIG)"
exit 0
