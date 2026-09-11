#!/usr/bin/env bash
# Reproducible CpuBudget measurement harness.
#
# Commands are supplied by the caller because the still/pipe/chunked fixtures
# belong to the active content pack.  This script owns the measurement protocol
# and report schema, not the fixture definitions.
#
# Example:
#   bash tools/measure_cpu_budget.sh --out docs/baselines/cpu-budget-...txt \
#     --still-cmd 'chronon render Scene --frame 0 -o /tmp/still.png' \
#     --pipe-cmd 'chronon render Scene --frames 0-59 --ffmpeg-mode pipe -o /tmp/pipe.mp4' \
#     --chunked-cmd 'chronon render Scene --frames 0-59 --ffmpeg-mode png --chunks 4 -o /tmp/chunked.mp4'

set -euo pipefail

OUT_FILE=""
ARTIFACT_DIR="${CHRONON3D_CPU_BUDGET_ARTIFACT_DIR:-/tmp/chronon-cpu-budget}"
STILL_CMD=""
PIPE_CMD=""
CHUNKED_CMD=""

usage() {
    sed -n '2,14p' "$0"
    cat >&2 <<'EOF'

Options:
  --out FILE          report path (default: docs/baselines/cpu-budget-UTC.txt)
  --artifacts DIR     directory containing command outputs/sidecars
  --still-cmd CMD     one-frame render command
  --pipe-cmd CMD      interleaved pipe render+encode command
  --chunked-cmd CMD   chunked render then encode command
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out)        OUT_FILE="$2"; shift 2 ;;
        --artifacts)  ARTIFACT_DIR="$2"; shift 2 ;;
        --still-cmd)  STILL_CMD="$2"; shift 2 ;;
        --pipe-cmd)   PIPE_CMD="$2"; shift 2 ;;
        --chunked-cmd) CHUNKED_CMD="$2"; shift 2 ;;
        -h|--help)    usage; exit 0 ;;
        *) echo "[ERROR] unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ -z "$OUT_FILE" ]]; then
    OUT_FILE="$(pwd)/docs/baselines/cpu-budget-$(date -u +%Y%m%dT%H%M%SZ).txt"
fi
[[ -n "$STILL_CMD" || -n "$PIPE_CMD" || -n "$CHUNKED_CMD" ]] || {
    echo "[ERROR] provide at least one of --still-cmd, --pipe-cmd, --chunked-cmd" >&2
    exit 2
}

mkdir -p "$ARTIFACT_DIR" "$(dirname "$OUT_FILE")"

python3 - "$OUT_FILE" "$ARTIFACT_DIR" "$STILL_CMD" "$PIPE_CMD" "$CHUNKED_CMD" <<'PY'
import json
import os
import pathlib
import re
import subprocess
import sys
import time

out_file = pathlib.Path(sys.argv[1])
artifact_dir = pathlib.Path(sys.argv[2])
commands = [("still", sys.argv[3]), ("pipe", sys.argv[4]), ("chunked", sys.argv[5])]

def latest_sidecar(before):
    files = [p for p in artifact_dir.rglob("*.timing.json")
             if p not in before and p.is_file()]
    return max(files, key=lambda p: p.stat().st_mtime_ns) if files else None

def parse_time(path):
    values = {}
    if not path.is_file(): return values
    for line in path.read_text(errors="replace").splitlines():
        if ":" not in line: continue
        key, value = line.split(":", 1)
        key = key.strip()
        value = value.strip()
        if key == "Elapsed (wall clock) time (h:mm:ss or m:ss)":
            parts = value.split(":")
            try:
                if len(parts) == 3: values["wall_ms"] = (int(parts[0])*3600 + int(parts[1])*60 + float(parts[2]))*1000
                elif len(parts) == 2: values["wall_ms"] = (int(parts[0])*60 + float(parts[1]))*1000
            except ValueError: pass
        elif key == "Maximum resident set size (kbytes)":
            try: values["peak_rss_kb"] = int(value)
            except ValueError: pass
        elif key == "Percent of CPU this job got":
            try: values["cpu_percent"] = float(value.rstrip("%"))
            except ValueError: pass
    return values

def json_number(data, *path):
    for key in path:
        if not isinstance(data, dict): return None
        data = data.get(key)
    return data if isinstance(data, (int, float)) and not isinstance(data, bool) else None

rows = []
for name, command in commands:
    if not command: continue
    before_sidecars = set(artifact_dir.rglob("*.timing.json"))
    stdout = artifact_dir / f"{name}.stdout.log"
    stderr = artifact_dir / f"{name}.stderr.log"
    time_file = artifact_dir / f"{name}.time.txt"
    started = time.monotonic()
    with stdout.open("w") as out, stderr.open("w") as err, time_file.open("w") as tm:
        proc = subprocess.run(["/usr/bin/time", "-v", "bash", "-lc", command],
                              stdout=out, stderr=subprocess.PIPE, text=True)
        err.write(proc.stderr)
        tm.write(proc.stderr)
    elapsed_ms = (time.monotonic() - started) * 1000.0
    metrics = parse_time(time_file)
    metrics.setdefault("wall_ms", elapsed_ms)
    sidecar = latest_sidecar(before_sidecars)
    sidecar_data = {}
    if sidecar:
        try: sidecar_data = json.loads(sidecar.read_text())
        except (OSError, json.JSONDecodeError): sidecar = None
    row = {
        "mode": name, "exit_code": proc.returncode, "command": command,
        "sidecar": str(sidecar) if sidecar else None,
        "wall_ms": metrics.get("wall_ms"),
        "render_ms": json_number(sidecar_data, "job", "render_loop_wall_ms"),
        "encode_ms": json_number(sidecar_data, "job", "encoder_finalize_ms"),
        "job_wall_ms": json_number(sidecar_data, "job", "job_wall_ms"),
        "cpu_percent": metrics.get("cpu_percent"),
        "peak_rss_kb": metrics.get("peak_rss_kb"),
    }
    if row["render_ms"] is None: row["render_ms"] = json_number(sidecar_data, "exclusive_wall_timeline", "render_loop_ms")
    if row["encode_ms"] is None: row["encode_ms"] = json_number(sidecar_data, "exclusive_wall_timeline", "encoder_drain_finalize_ms")
    if row["job_wall_ms"] is None: row["job_wall_ms"] = json_number(sidecar_data, "exclusive_wall_timeline", "process_wall_ms")
    if row["render_ms"] is not None and row["encode_ms"] is not None:
        total = row["render_ms"] + row["encode_ms"]
        row["encode_ratio"] = row["encode_ms"] / total if total else 0.0
    else:
        row["encode_ratio"] = None
    rows.append(row)

decision = "INSUFFICIENT_DATA"
ratios = [r["encode_ratio"] for r in rows if r["encode_ratio"] is not None]
if ratios:
    decision = "EXTEND_CPU_BUDGET_BOUNDED_PIPELINE" if max(ratios) > 0.25 else "KEEP_SEQUENTIAL_CPU_BUDGET"

lines = [
    "# Chronon3d CpuBudget measurement",
    "",
    f"commit: {subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()}",
    f"generated_utc: {time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}",
    "protocol: /usr/bin/time -v; timing sidecar authoritative when present",
    "",
    "mode | exit | wall_ms | render_ms | encode_ms | encode_ratio | cpu_percent | peak_rss_kb | sidecar",
    "--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---",
]
for row in rows:
    def f(key):
        value = row.get(key)
        return "NA" if value is None else (f"{value:.4f}" if isinstance(value, float) else str(value))
    lines.append(" | ".join([row["mode"], str(row["exit_code"]), f("wall_ms"), f("render_ms"), f("encode_ms"), f("encode_ratio"), f("cpu_percent"), f("peak_rss_kb"), row["sidecar"] or "NA"]))
lines += ["", f"decision: {decision}", "", "raw JSON:", json.dumps(rows, indent=2, sort_keys=True)]
out_file.write_text("\n".join(lines) + "\n")
print(f"CPU_BUDGET_REPORT={out_file}")
print(f"CPU_BUDGET_DECISION={decision}")
if any(row["exit_code"] != 0 for row in rows): raise SystemExit(1)
PY
