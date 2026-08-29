#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
OUT="${1:-/tmp/canon_profile_02}"
CLI="${CHRONON3D_CLI:-$ROOT/build/chronon/linux-video-fast-dev/apps/chronon3d_cli/chronon3d_cli}"
CSV="${OUT}.hardware.csv"
LOG="${OUT}.log"
TRACE="${CHRONON3D_PROFILE_TRACE:-}"
HOT_PATH_MODE="${CHRONON3D_HOT_PATH_MODE:-require_gpu_native}"
trace_args=()
if [[ -n "$TRACE" ]]; then
  trace_args=(--trace "$TRACE")
fi

if [[ ! -x "$CLI" ]]; then
  echo "missing CLI: $CLI" >&2
  exit 2
fi

printf 'timestamp,gpu_utilization_percent,vram_used_mb,vram_total_mb,nvdec_utilization_percent,nvenc_utilization_percent,gpu_temperature_c,cpu_process_percent,rss_mb\n' > "$CSV"

sample_tree() {
  local root="$1"
  python3 - "$root" <<'PY'
import os, subprocess, sys
root=int(sys.argv[1]); seen={root}; todo=[root]; cpu=0.0; rss=0.0
try:
    rows=subprocess.check_output(['ps','-eo','pid=,ppid=,%cpu=,rss='],text=True).splitlines()
    children={}
    for row in rows:
        p=row.split();
        if len(p)==4: children.setdefault(int(p[1]),[]).append((int(p[0]),float(p[2]),float(p[3])))
    while todo:
        parent=todo.pop()
        for pid,c,r in children.get(parent,[]):
            if pid not in seen:
                seen.add(pid); todo.append(pid); cpu+=c; rss+=r
    own=subprocess.check_output(['ps','-p',str(root),'-o','%cpu=,rss='],text=True).split()
    cpu+=float(own[0]); rss+=float(own[1])
except Exception:
    pass
print(f'{cpu:.3f},{rss/1024.0:.3f}')
PY
}

PLAN="${CHRONON3D_PLAN:-test_renders/matt_damon_1080p/plan_matt_damon_1080p.json}"

"$CLI" render \
  --plan "$PLAN" \
  --backend vulkan --hardware nvenc --encoder-backend native \
  --assets-root "$ROOT" --gpu-hot-path-mode "$HOT_PATH_MODE" \
  "${trace_args[@]}" \
  -o "${OUT}.mp4" >"$LOG" 2>&1 &
pid=$!

while kill -0 "$pid" 2>/dev/null; do
  ts="$(date +%s%3N)"
  gpu="$(nvidia-smi --query-gpu=utilization.gpu,memory.used,memory.total,utilization.decoder,utilization.encoder,temperature.gpu --format=csv,noheader,nounits 2>/dev/null | head -n1 | tr -d ' ' || true)"
  [[ -n "$gpu" ]] || gpu=",,,,,"
  cpu_rss="$(sample_tree "$pid")"
  printf '%s,%s,%s\n' "$ts" "$gpu" "$cpu_rss" >> "$CSV"
  sleep 0.5
done

wait "$pid"
python3 tools/profile_real_gpu_render.py "${OUT}.mp4.timing.json" \
  --hardware-csv "$CSV" \
  --json "${OUT}.profile.json" \
  --markdown "${OUT}.profile.md"
echo "profile=${OUT}.profile.json"
