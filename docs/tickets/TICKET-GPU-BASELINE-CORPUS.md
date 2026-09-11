# TICKET-GPU-BASELINE-CORPUS — Official GPU baseline runner

## Stato

IMPLEMENTED / MEASUREMENT-READY (2026-09-11)

## Contract

`tools/benchmark_gpu_baseline.sh` is the single runner for reproducible GPU
baseline measurements. It invokes the canonical `render` command with fixed
backend, encoder, frame count and framebuffer warm-up options. It reads only
the authoritative `*.timing.json` sidecar for Chronon timings and retains the
process log only for diagnostics.

Each case records commit/parameters, output and sidecar paths, exit status,
frame count, execution path, startup/prepare/encode/finalize timings,
prewarm/steady-state windows, and exclusive wall-time attribution.

The runner rejects a case when the sidecar is missing, the frame count differs
from the requested count, or less than 95% of process wall time is attributed.
This prevents an FPS number with unexplained wall time from becoming a
baseline.

## Usage

```bash
bash tools/benchmark_gpu_baseline.sh \
  --out artifacts/gpu-baseline-23829045a --frames 120 --warmup 10 \
  --case GPU_TEXT_01=Clean --case GPU_TEXT_02=Pop \
  --case GPU_TEXT_03=Neon --case GPU_TEXT_04=Tracking \
  --case GPU_TEXT_05=Glitch
```

The typewriter names are supplied by the active content pack; Chronon does
not register fake benchmark scenes. With no `--case`, the runnable built-in
smoke case is `B00=DarkGridBackground`.

## Evidence

The runner was syntax-checked and executed on `23829045a` with a 12-frame
GPU/NVENC smoke case. It produced a sidecar, 12/12 frames, and 99.37%
exclusive wall-time attribution. The full B00–B11 and GPU_TEXT corpus remains
measurement work on the host that provides those fixtures.
