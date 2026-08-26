#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# bench/run_perf_baseline.sh — Chronon Performance Baseline runner (BENCH-1..5)
#
# TICKET-PERF-BASELINE-V1 — produces the official per-benchmark saturation
# report + a flat suite JSON/TXT summary, recording:
#   frame_ms (p50/p95/p99), gpu_execute_us, gpu_wait_us,
#   uploads/frame, upload_bytes/frame, readback_bytes/frame,
#   vk submissions/frame, VMA allocations after warmup,
#   physical surfaces peak, VRAM usage bytes,
#   CPU% (from /proc/self + cgroup), GPU% (nvidia-smi, best-effort),
#   encode fps (from video-export .timing.json sidecar, optional).
#
# Modeled on bench/run_perf_bench.sh (governor + taskset + warmup + perf)
# but scoped to the 5 canonical benchmarks with a suite-level summary.
#
# Cat-3 minimal-surface: ZERO new SDK API. Reuses chronon3d_cli benchmark
# --saturation --report-json, tools/benchmark_host_info.sh, python3 stdlib.
#
# Usage:
#   bash bench/run_perf_baseline.sh                          # all 5 benches
#   bash bench/run_perf_baseline.sh --bench BENCH-1,BENCH-3  # subset
#   bash bench/run_perf_baseline.sh --duration 30 --repetitions 3
#   bash bench/run_perf_baseline.sh --output-dir /tmp/chronon_perf
#
# Env overrides:
#   CHRONON3D_CLI             path to chronon3d_cli (default build-fast)
#   CHRONON3D_BENCH_DURATION  seconds per benchmark          (default 30)
#   CHRONON3D_BENCH_WARMUP    warmup seconds                 (default 30)
#   CHRONON3D_BENCH_REPS      repetitions per benchmark      (default 3)
# ═══════════════════════════════════════════════════════════════════════════

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# ── Defaults ─────────────────────────────────────────────────────────────
DURATION="${CHRONON3D_BENCH_DURATION:-30}"
WARMUP="${CHRONON3D_BENCH_WARMUP:-30}"
REPETITIONS="${CHRONON3D_BENCH_REPS:-3}"
OUTPUT_DIR="${CHRONON3D_BENCH_OUTPUT_DIR:-/tmp/chronon_perf}"
CLI_BIN="${CHRONON3D_CLI:-${REPO_ROOT}/build/fast/apps/chronon3d_cli/chronon3d_cli}"
MANIFEST="${REPO_ROOT}/bench/perf_baseline_v1.json"
HOST_INFO="${REPO_ROOT}/tools/benchmark_host_info.sh"
SELECTED="BENCH-1,BENCH-2,BENCH-3,BENCH-4,BENCH-5"
MACHINE_ID="${CHRONON3D_BENCH_MACHINE:-unknown}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --benches|--bench) SELECTED="$2"; shift 2 ;;
        --duration|--seconds) DURATION="$2"; shift 2 ;;
        --warmup) WARMUP="$2"; shift 2 ;;
        --repetitions|--reps) REPETITIONS="$2"; shift 2 ;;
        --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
        --machine) MACHINE_ID="$2"; shift 2 ;;
        -h|--help)
            sed -n '2,30p' "$0"; exit 0 ;;
        *) echo "[ERROR] run_perf_baseline.sh: unknown arg: $1" >&2; exit 2 ;;
    esac
done

# ── Helpers ─────────────────────────────────────────────────────────────
_warn() { echo "[WARN] $(basename "$0"): $*" >&2; }
_info() { echo "[INFO] $(basename "$0"): $*" >&2; }
_err()  { echo "[ERROR] $(basename "$0"): $*" >&2; }

# Parse manifest via python3 (single SSoT).
benches_py="$(MANIFEST="$MANIFEST" python3 - <<'PY'
import json, os, sys
try:
    with open(os.environ["MANIFEST"], "r", encoding="utf-8") as fh:
        data = json.load(fh)
except Exception as exc:
    print(f"[ERROR] cannot read manifest: {exc}", file=sys.stderr)
    sys.exit(2)
for b in data.get("benchmarks", []):
    print(f"{b['id']}|{b['composition']}|{b['content']}")
PY
)"
if [[ $? -ne 0 ]]; then
    _err "manifest parse failed"
    exit 2
fi

declare -A COMP_BY_ID
while IFS='|' read -r id comp content; do
    COMP_BY_ID["$id"]="$comp"
done <<< "$benches_py"

if [[ ! -x "$CLI_BIN" ]]; then
    _err "chronon3d_cli not found at $CLI_BIN (set CHRONON3D_CLI or build first)"
    exit 2
fi

mkdir -p "$OUTPUT_DIR"
COMMIT="$(git -C "$REPO_ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
HOST_JSON="$(bash "$HOST_INFO" --json 2>/dev/null || echo '{"host":{}}')"
# Unwrap the host-info envelope: benchmark_host_info.sh emits {"host":{...}}.
HOST_INNER="$(echo "$HOST_JSON" | python3 -c 'import json,sys; print(json.dumps(json.load(sys.stdin).get("host", {})))' 2>/dev/null || echo '{}')"
MACHINE="${MACHINE_ID:-$(echo "$HOST_INNER" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("cpu_model","unknown"))' 2>/dev/null)}"

_info "suite start: benches=$SELECTED duration=${DURATION}s warmup=${WARMUP}s reps=${REPETITIONS} output=$OUTPUT_DIR"
_info "cli: $CLI_BIN"
_info "commit: $COMMIT"

# ── Options loop for governor/taskset/warmup per bench ──────────────────
# (kept declarative; the existing run_perf_bench.sh handles cpupower/taskset
#  details — here we focus on the report generation.)

SUITE_OUT="$OUTPUT_DIR/perf_baseline_${COMMIT}.json"
REPORT_TXT="$OUTPUT_DIR/perf_baseline_${COMMIT}.txt"

{
    echo "Chronon Performance Baseline — $COMMIT"
    echo "machine: $MACHINE"
    echo "host: $(echo "$HOST_JSON" | python3 -c 'import json,sys; h=json.load(sys.stdin)["host"]; print(h.get("cpu_model","?"), h.get("logical_cores","?"), h.get("ram_gb","?"), h.get("kernel","?"), h.get("cpu_governor","?"))' 2>/dev/null)"
    echo "duration=${DURATION}s warmup=${WARMUP}s reps=${REPETITIONS}"
    echo "executed: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo
} > "$REPORT_TXT"

BENCH_ENTRIES=()
for b in $(echo "$SELECTED" | tr ',' ' '); do
    if [[ -z "${COMP_BY_ID[$b]:-}" ]]; then
        _warn "unknown bench id: $b (valid: ${!COMP_BY_ID[*]}) — skipping"
        continue
    fi
    COMP="${COMP_BY_ID[$b]}"
    _info "== $b ($COMP): duration=${DURATION}s warmup=${WARMUP}s reps=${REPETITIONS} =="
    REP_JSON=""
    for ((r=1; r<=REPETITIONS; r++)); do
        _info "  repetition $r/$REPETITIONS"
        CUR="$OUTPUT_DIR/${b}_${COMMIT}_r${r}.json"
        GPU_SAMPLES="$OUTPUT_DIR/${b}_${COMMIT}_r${r}.gpu_samples"
        : > "$GPU_SAMPLES"
        "$CLI_BIN" benchmark --scene "$COMP" --duration "$DURATION" \
               --saturation --report-json "$CUR" --backend "${CHRONON3D_BENCH_BACKEND:-auto}" >> "$REPORT_TXT" 2>&1 &
        BENCH_PID=$!
        while kill -0 "$BENCH_PID" 2>/dev/null; do
            if command -v nvidia-smi >/dev/null 2>&1; then
                nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits 2>/dev/null \
                    | awk 'NF { print $1; exit }' >> "$GPU_SAMPLES" || true
            fi
            sleep 1
        done
        if ! wait "$BENCH_PID"; then
            _err "benchmark failed for $b repetition $r"
            exit 1
        fi
        if [[ -f "$CUR" ]]; then
            python3 - "$CUR" "$GPU_SAMPLES" <<'PY'
import json
import sys
report_path, samples_path = sys.argv[1:]
with open(report_path, encoding="utf-8") as fh:
    report = json.load(fh)
values = []
with open(samples_path, encoding="utf-8") as fh:
    for line in fh:
        try:
            values.append(float(line.strip()))
        except ValueError:
            pass
utilization = report.setdefault("utilization", {})
if values:
    utilization["gpu_percent"] = sum(values) / len(values)
    utilization["gpu_percent_status"] = "NVIDIA_SMI_INTERVAL_MEAN"
else:
    utilization["gpu_percent"] = None
    utilization["gpu_percent_status"] = "NOT_AVAILABLE"
with open(report_path, "w", encoding="utf-8") as fh:
    json.dump(report, fh, indent=2)
    fh.write("\n")
PY
            REP_JSON="$CUR"
        fi
    done
    if [[ -z "$REP_JSON" ]]; then
        _err "no valid saturation JSON for $b"
        exit 1
    fi
    BENCH_ENTRIES+=("$b|$REP_JSON")

    # Append a compact table line to the TXT report.
    python3 - "$b" "$REP_JSON" >> "$REPORT_TXT" <<'PY'
import json, sys
bench_id, path = sys.argv[1], sys.argv[2]
with open(path, encoding="utf-8") as fh:
    d = json.load(fh)
fps = d.get("fps", 0.0)
p50 = d.get("p50_ms", 0.0)
p95 = d.get("p95_ms", 0.0)
gpu = d.get("gpu", {})
exec_us = gpu.get("gpu_execute_us_per_frame", "N/A")
wait_us = gpu.get("gpu_wait_cpu_us_per_frame", "N/A")
upd = gpu.get("gpu_upload_bytes_per_frame", "N/A")
rb = gpu.get("gpu_readback_bytes_per_frame", "N/A")
sub = gpu.get("vk_submissions_per_frame", "N/A")
vma = gpu.get("vma_allocation_count_after", "N/A")
print(f"{bench_id}: p50={p50:.2f}ms p95={p95:.2f}ms fps={fps:.1f} "
      f"gpu_exec={exec_us}us wait={wait_us}us uploads={upd}B readback={rb}B "
      f"vk_submits={sub} vma_alloc={vma}")
PY
done

python3 - "$SUITE_OUT" "$COMMIT" "$MACHINE" "$HOST_INNER" "$WARMUP" "$DURATION" "$REPETITIONS" "${BENCH_ENTRIES[@]}" <<'PY'
import json
import sys
from datetime import datetime, timezone

suite_path, commit, machine, host_json, warmup, duration, repetitions = sys.argv[1:8]
entries = sys.argv[8:]
benchmarks = {}
for entry in entries:
    bench_id, report_path = entry.split("|", 1)
    with open(report_path, encoding="utf-8") as fh:
        benchmarks[bench_id] = json.load(fh)

suite = {
    "schema": "chronon3d.perf_baseline.v1",
    "commit": commit,
    "machine": machine,
    "host": json.loads(host_json),
    "runner": "bench/run_perf_baseline.sh",
    "timestamp_utc": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
    "warmup_s": int(warmup),
    "duration_s": int(duration),
    "repetitions": int(repetitions),
    "benchmarks": benchmarks,
}
with open(suite_path, "w", encoding="utf-8") as fh:
    json.dump(suite, fh, indent=2)
    fh.write("\n")
PY

# Validate the suite JSON parses. Missing or failed benchmarks are fatal:
# a partial suite is not a baseline.

if ! python3 -c 'import json,sys; json.load(open(sys.argv[1]))' "$SUITE_OUT"; then
    _err "suite JSON is not valid: $SUITE_OUT (parse error — see above)"
    exit 1
fi

_info "per-bench reports + suite summary written to $OUTPUT_DIR"
_info "suite JSON : $SUITE_OUT"
_info "report TXT : $REPORT_TXT"

# The saturation command records process CPU utilization in each report.
# GPU utilization and encode FPS are explicitly null for this render-only
# command; video-export timing sidecars remain the authority for encode FPS.
python3 - "$SUITE_OUT" "$SELECTED" <<'PY'
import json, sys
path, selected = sys.argv[1:]
with open(path, encoding="utf-8") as fh:
    data = json.load(fh)
required = set(selected.split(","))
actual = set(data.get("benchmarks", {}))
missing = sorted(required - actual)
if missing:
    raise SystemExit("missing canonical benchmarks: " + ", ".join(missing))
data["metric_contract"] = {
    "frame_ms": "p50_ms/p95_ms/p99_ms",
    "cpu_percent": "utilization.cpu_percent",
    "gpu_percent": "utilization.gpu_percent (nvidia-smi interval mean or null)",
    "gpu_execute_us": "gpu.gpu_execute_us_per_frame",
    "gpu_wait_us": "gpu.gpu_wait_cpu_us_per_frame",
    "uploads_per_frame": "gpu.gpu_upload_count_per_frame",
    "upload_bytes_per_frame": "gpu.gpu_upload_bytes_per_frame",
    "readback_bytes_per_frame": "gpu.gpu_readback_bytes_per_frame",
    "vk_submissions_per_frame": "gpu.vk_submissions_per_frame",
    "vma_allocations_after_warmup": "gpu.vma_allocation_count_after",
    "physical_surfaces": "gpu.physical_surfaces_peak",
    "peak_vram": "gpu.vma_usage_bytes_after",
    "encode_fps": "null for render-only saturation; video timing sidecar required",
}
with open(path, "w", encoding="utf-8") as fh:
    json.dump(data, fh, indent=2)
    fh.write("\n")
PY
_info "suite complete: selected canonical benchmarks present (${SELECTED})"
_info "GPU utilization uses nvidia-smi interval means when available; encode FPS requires a video timing sidecar"

exit 0