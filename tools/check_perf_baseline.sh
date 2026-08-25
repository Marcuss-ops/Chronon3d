#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# tools/check_perf_baseline.sh — Chronon Performance Baseline budget gate
#
# TICKET-PERF-BASELINE-V1 — validates a suite produced by
# bench/run_perf_baseline.sh against the budgets declared in
# bench/perf_baseline_v1.json (frame_p50_ms_max, frame_p95_ms_max,
# gpu_execute_us_per_frame_max, gpu_readback_bytes_per_frame_max).
#
# Exit codes:
#   0 = PASS     (all measured metrics within budgets)
#   1 = FAIL     (at least one metric exceeds its budget)
#   2 = BLOCKED  (missing inputs, unparseable JSON, budgets not locked)
#
# Usage:
#   bash tools/check_perf_baseline.sh --suite <perf_baseline_<sha>.json>
#   bash tools/check_perf_baseline.sh --suite <...> --manifest bench/perf_baseline_v1.json
#
# Cat-3 minimal-surface: ZERO new SDK API. Pure stdlib python3 + bash.
# ═══════════════════════════════════════════════════════════════════════════

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

SUITE=""
MANIFEST="${REPO_ROOT}/bench/perf_baseline_v1.json"
VERBOSE=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --suite) SUITE="$2"; shift 2 ;;
        --manifest) MANIFEST="$2"; shift 2 ;;
        --verbose) VERBOSE=true; shift ;;
        -h|--help)
            sed -n '2,20p' "$0"; exit 0 ;;
        *) echo "[ERROR] check_perf_baseline.sh: unknown arg: $1" >&2; exit 2 ;;
    esac
done

_warn() { echo "[WARN] $(basename "$0"): $*" >&2; }
_info() { echo "[INFO] $(basename "$0"): $*" >&2; }
_err()  { echo "[ERROR] $(basename "$0"): $*" >&2; }
_fail() { echo "[FAIL] $(basename "$0"): $*" >&2; }

if [[ -z "$SUITE" ]]; then
    _err "--suite is required (path to perf_baseline_<commit>.json)"
    exit 2
fi
if [[ ! -f "$SUITE" ]]; then
    _err "suite not found: $SUITE"
    exit 2
fi
if [[ ! -f "$MANIFEST" ]]; then
    _err "manifest not found: $MANIFEST"
    exit 2
fi

# ── Compare via python3 (stdlib only) ───────────────────────────────────
python3 - "$SUITE" "$MANIFEST" "$VERBOSE" <<'PY'
import json, sys

suite_path, manifest_path, verbose = sys.argv[1], sys.argv[2], (sys.argv[3] == "true")

with open(manifest_path, encoding="utf-8") as fh:
    manifest = json.load(fh)
with open(suite_path, encoding="utf-8") as fh:
    suite = json.load(fh)

budgets_by_id = {b["id"]: b.get("budgets", {}) for b in manifest.get("benchmarks", [])}
suites_by_id = suite.get("benchmarks", {})
if not isinstance(suites_by_id, dict):
    print("[FAIL] suite 'benchmarks' is not an object", file=sys.stderr)
    sys.exit(1)

failures = []
blocked = []
checked = 0

for bench_id, budget in budgets_by_id.items():
    if budget.get("status") != "LOCKED":
        blocked.append(f"{bench_id}: budgets not LOCKED (status={budget.get('status')!r})")
        continue
    if bench_id not in suites_by_id or not isinstance(suites_by_id[bench_id], dict):
        blocked.append(f"{bench_id}: missing from suite")
        continue
    data = suites_by_id[bench_id]
    gpu = data.get("gpu", {}) if isinstance(data, dict) else {}
    frame_times = data.get("frame_times_ms", []) if isinstance(data, dict) else []
    measured_p50 = data.get("p50_ms")
    measured_p95 = data.get("p95_ms")
    metrics = {
        "frame_p50_ms": measured_p50,
        "frame_p95_ms": measured_p95,
        "gpu_execute_us_per_frame": gpu.get("gpu_execute_us_per_frame"),
        "gpu_readback_bytes_per_frame": gpu.get("gpu_readback_bytes_per_frame"),
    }
    for metric, cap in budget.items():
        if metric == "status":
            continue
        if not isinstance(cap, (int, float)):
            continue
        # Budget keys carry a `_max` suffix (frame_p50_ms_max); the suite
        # uses the raw metric name (frame_p50_ms).
        metric_name = metric[:-4] if metric.endswith("_max") else metric
        value = metrics.get(metric_name)
        if value is None or not isinstance(value, (int, float)):
            continue
        checked += 1
        if value > cap:
            failures.append(f"{bench_id} {metric_name}: {value:.3f} > budget {cap:.3f}")
        elif verbose:
            print(f"[INFO] check_perf_baseline: {bench_id} {metric_name}: {value:.3f} <= {cap:.3f}")

if blocked:
    print("[BLOCKED] " + "; ".join(blocked), file=sys.stderr)
    sys.exit(2)
if failures:
    print("[FAIL] " + "\n".join(failures), file=sys.stderr)
    sys.exit(1)
print(f"[INFO] check_perf_baseline: {checked} metric checks passed")
sys.exit(0)
PY
exit $?