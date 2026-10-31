#!/usr/bin/env bash
set -uo pipefail
cd "$(dirname "$0")/../../.." || exit 1
p=docs/evidence/camera-closure-final
cli=build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli
status=0
for mode in sharp dof shutter; do
  for run in 1 2 3 4 5; do
    base="$p/benchmark-${mode}-${run}"
    printf '%s start %s %s\n' "$(date -Is)" "$mode" "$run" >> "$p/benchmark-progress.log"
    (while :; do printf '%s,' "$(date -Is)"; nvidia-smi --query-gpu=memory.used,memory.total,utilization.gpu --format=csv,noheader,nounits; sleep 5; done) > "$base.vram.csv" 2>&1 &
    sampler=$!
    CHRONON3D_VULKAN_VALIDATION=1 "$cli" render --plan "$p/optical_${mode}.plan.json" --assets-root "$PWD" --backend vulkan --gpu-hot-path-mode require_gpu_native --hardware nvenc --encoder-backend native -o "$base.mp4" > "$base.log" 2>&1
    rc=$?
    kill "$sampler" 2>/dev/null || true
    wait "$sampler" 2>/dev/null || true
    printf '%s\n' "$rc" > "$base.exit"
    printf '%s end %s %s exit=%s\n' "$(date -Is)" "$mode" "$run" "$rc" >> "$p/benchmark-progress.log"
    if [ "$rc" -ne 0 ]; then status="$rc"; break 2; fi
  done
done
printf '%s\n' "$status" > "$p/benchmark.exit"
exit "$status"
