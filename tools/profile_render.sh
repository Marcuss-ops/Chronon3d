#!/usr/bin/env bash
set -uo pipefail

usage() {
  echo "Usage: $0 [--out DIR] [--timing FILE] -- COMMAND [ARGS...]" >&2
}

out_dir=""
timing_file=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) out_dir="$2"; shift 2 ;;
    --timing) timing_file="$2"; shift 2 ;;
    --) shift; break ;;
    -h|--help) usage; exit 0 ;;
    *) usage; exit 2 ;;
  esac
done
[[ $# -gt 0 ]] || { usage; exit 2; }

if [[ -z "$out_dir" ]]; then
  out_dir="profile-$(date -u +%Y%m%dT%H%M%SZ)"
fi
mkdir -p "$out_dir"
printf 'command=' > "$out_dir/command.txt"
printf '%q ' "$@" >> "$out_dir/command.txt"
printf '\n' >> "$out_dir/command.txt"

# Prefer repository-local profiler bundles, then fall back to system installs.
# This keeps profiling reproducible on workstations where sudo is unavailable.
tool_root="${CHRONON3D_PROFILERS_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/profilers" 2>/dev/null && pwd || true)}"
if [[ ! -d "$tool_root/nsight-systems" && -d "/home/pierone/.local/share/chronon3d/profilers" ]]; then
  tool_root="/home/pierone/.local/share/chronon3d/profilers"
fi
nsys_tool=""
tracy_capture_tool=""
if [[ -n "$tool_root" ]]; then
  nsys_tool="$(find "$tool_root/nsight-systems" -type f -name nsys -perm -111 -print -quit 2>/dev/null || true)"
  tracy_capture_tool="$(find "$tool_root" -type f -name tracy-capture -perm -111 -print -quit 2>/dev/null || true)"
fi
[[ -n "$nsys_tool" ]] || nsys_tool="$(command -v nsys 2>/dev/null || true)"
[[ -n "$tracy_capture_tool" ]] || tracy_capture_tool="$(command -v tracy-capture 2>/dev/null || true)"

run_capture() {
  local name="$1"; shift
  echo "[profile] $name"
  "$@" >"$out_dir/$name.stdout.txt" 2>"$out_dir/$name.stderr.txt"
  local status=$?
  printf '%s\n' "$status" > "$out_dir/$name.exit_code"
  return 0
}

if command -v /usr/bin/time >/dev/null 2>&1; then
  run_capture time /usr/bin/time -v "$@"
else
  printf '%s\n' 'NOT_AVAILABLE: /usr/bin/time' > "$out_dir/time.status"
fi

if command -v perf >/dev/null 2>&1; then
  run_capture perf-stat perf stat -x, -e cycles,instructions,cache-misses,branches,branch-misses -- "$@"
  run_capture perf-record perf record -F 999 -g -o "$out_dir/perf.data" -- "$@"
else
  printf '%s\n' 'NOT_AVAILABLE: perf' > "$out_dir/perf.status"
fi

if [[ -n "$nsys_tool" ]]; then
  # Nsight Systems 2026 no longer accepts the legacy nvml trace selector.
  run_capture nsight "$nsys_tool" profile --trace=vulkan,osrt --sample=process-tree \
    --output "$out_dir/nsys" --force-overwrite true -- "$@"
else
  printf '%s\n' 'NOT_AVAILABLE: nsys' > "$out_dir/nsys.status"
fi

if [[ -n "$tracy_capture_tool" ]]; then
  if [[ -n "${TRACY_HOST:-}" ]]; then
    run_capture tracy "$tracy_capture_tool" -a "$TRACY_HOST" -o "$out_dir/render.tracy" -f
  else
    printf '%s\n' 'NOT_RUN: set TRACY_HOST to capture a running Tracy instrumented process' > "$out_dir/tracy.status"
  fi
else
  printf '%s\n' 'NOT_AVAILABLE: tracy-capture' > "$out_dir/tracy.status"
fi

if [[ -n "$timing_file" && -f "$timing_file" ]]; then
  if command -v jq >/dev/null 2>&1; then
    jq '{render_loop_wall_ms: .job.render_loop_wall_ms, prepare_ms: .job.prepare_ms,
         startup_ms: .startup_breakdown.total_startup_ms,
         image_convert_ms: .job.image.convert_ms,
         node_dispatch_ms: .job.cpu_breakdown.node_dispatch_ms,
         node_overhead_ms: .job.cpu_breakdown.node_overhead_ms,
         encoder_finalize_ms: .job.encoder_finalize_ms,
         wall_unaccounted_ms: .exclusive_wall_timeline.unaccounted_ms}' \
      "$timing_file" > "$out_dir/chronon-phase-summary.json"
  else
    cp "$timing_file" "$out_dir/chronon-timing.json"
    printf '%s\n' 'jq unavailable: copied timing receipt without reduction' > "$out_dir/chronon-phase-summary.status"
  fi
fi

echo "[profile] outputs: $out_dir"
exit 0
