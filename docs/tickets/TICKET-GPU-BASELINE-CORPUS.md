# TICKET-GPU-BASELINE-CORPUS — Official GPU baseline runner

## Stato

IMPLEMENTED / MANIFEST-LOCKED / HOST-CERTIFICATION-PENDING (2026-09-11)

## Contract

`tools/benchmark_gpu_baseline.sh` is the single runner for reproducible GPU
baseline measurements. It invokes the canonical `render` command with fixed
backend, encoder, frame count and framebuffer warm-up options. It reads only
the authoritative `*.timing.json` sidecar for Chronon timings and retains the
process log only for diagnostics.

The versioned case list is
`docs/benchmarks/gpu-corpus-v1.json`. Pass it with `--corpus` to run the
complete B00–B11 + GPU_TEXT_01–05 set under one shared parameter set; use
repeatable `--case ID=SCENE` only for an explicit smoke or content-pack
override. The manifest names are resolved by the active content pack; it does
not add a second composition registry or fabricate fixtures.

Each case records commit/parameters, output and sidecar paths, exit status,
frame count, execution path, startup/prepare/encode/finalize timings,
prewarm/steady-state windows, and canonical `phase_attribution` with phase
sum, accounted percentage and explicit unaccounted remainder.

The runner rejects a case when the sidecar is missing, the frame count differs
from the requested count, or less than 95% of process wall time is attributed.
This prevents an FPS number with unexplained wall time from becoming a
baseline.

## Usage

```bash
bash tools/benchmark_gpu_baseline.sh \
  --out artifacts/gpu-baseline-<commit> --frames 120 --warmup 10 \
  --corpus docs/benchmarks/gpu-corpus-v1.json
```

The typewriter names are supplied by the active content pack; Chronon does
not register fake benchmark scenes. With no `--case`, the runnable built-in
smoke case is `B00=DarkGridBackground`.

## Evidence

The runner was syntax-checked and executed on `cfd178b59` with a 12-frame
`DarkGridBackground` GPU/NVENC smoke case. It produced a sidecar, 12/12
frames, and 99.028% wall-time attribution; `cuda_host_upload_bytes` was zero
in both prewarm and steady-state windows. The exact result is recorded in
`docs/baselines/gpu-baseline-cfd178b59.md`. The full B00–B11 and GPU_TEXT
corpus remains measurement work on the host that provides those fixtures; the
case contract is now versioned and gated by
`tests/test_gpu_corpus_manifest.py`.
