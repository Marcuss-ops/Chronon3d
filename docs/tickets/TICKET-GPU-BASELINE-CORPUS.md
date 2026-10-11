# TICKET-GPU-BASELINE-CORPUS — Official GPU baseline runner

Stato: IMPLEMENTED / MANIFEST-LOCKED / HOST-CERTIFICATION-PENDING (2026-09-11)

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

The authoritative `job.full_file_reread_count` field reports whether mux
finalization had to reread the completed file after a container seek/backpatch.
Certified append-only runs must report zero; non-zero values are an explicit
container fallback and cannot be mistaken for an incremental-hash result.

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

The current same-SHA hardware smoke is recorded in
[`gpu-b00-native-smoke-a16c86de2`](../evidence/gpu-b00-native-smoke-a16c86de2.md).
The 120-frame `DarkGridBackground` case on an RTX A4000/NVENC lane reached
99.247% wall-time attribution, with zero CPU readback, zero encoder staging
bytes, and zero GPU surface-copy frames. The full B00–B11 and GPU_TEXT corpus
remains pending the external fixture/content pack; the case contract is
versioned and gated by `tests/test_gpu_corpus_manifest.py`.

On 2026-09-11 the complete 17-case manifest was invoked against the current
`linux-video-test` build. All cases returned before producing a timing
sidecar because this checkout is configured with
`CHRONON3D_BUILD_CONTENT=OFF` and does not contain the external content
module. The runner records this as `command_failed_before_sidecar` with the
exit code and log path, rather than misclassifying it as a wall-time
attribution failure. This is a host/content-pack gate, not a baseline result.

## CPU contract re-run — 2026-10-11

- `python3 tests/test_gpu_corpus_manifest.py` passed. It verifies the manifest
  ID, all 17 unique case IDs, the B00–B11 and GPU_TEXT_01–05 ordering, and
  non-empty scene/category fields.
- `bash tools/benchmark_gpu_baseline.sh --help` returned the documented CLI
  options without starting a render.
- These checks verify the corpus definition and runner interface only. The
  full corpus still needs its external content pack and an actual GPU host.
