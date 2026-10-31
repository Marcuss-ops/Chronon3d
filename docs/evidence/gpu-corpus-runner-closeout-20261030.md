# GPU corpus runner closeout — 2026-10-30

## Goal

Make every locally reachable acceptance criterion for the versioned GPU
corpus enforceable and auditable, while leaving complete content-pack
certification explicitly blocked unless the canonical 17 cases are registered
and pass on the required host.

## Repository state and host

- Source HEAD: `9b5d23833ee48c1b5b8e0a3ca76b90303eb10825`.
- Worktree: dirty before and after this task; unrelated renderer, telemetry,
  tests, ticket, and evidence changes were already present. The official runner
  refuses certification on a dirty tree; local checks used
  `CHRONON3D_ALLOW_DIRTY=1` and are diagnostic only.
- Release CLI: `build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli`
  (`Chronon3d 0.1.0`, Release build configured with
  `CHRONON3D_BUILD_CONTENT=OFF`).
- GPU: NVIDIA RTX A4000, driver `595.99.02`, UUID
  `GPU-66101dcc-7b1c-e277-889c-c62462e0ac8e`, 16,376 MiB.
- FFmpeg: `4.4.2-0ubuntu0.22.04.1`.
- The active CLI lists built-in and template compositions but none of the 17
  manifest scenes. There is no repository `content/` pack directory.

## Implemented local gates

- Manifest version `1.2` specifies 17 cases, strict Vulkan/native NVENC GPU
  defaults, explicit acceptance requirements, and B09's 18,000-frame workload.
- Runner hashes the executable and manifest, records source commit/status,
  content-pack ID and host/GPU/FFmpeg identity; full-corpus certification is
  impossible for a single `--case` diagnostic run.
- Full-corpus preflight enumerates composition names and emits one result per
  canonical case before exiting non-zero if a case is not registered.
- Sidecar/output checks reject missing/invalid timing fields, attribution
  below 95%, non-zero rereads, frame_times/frames_total/steady-state/ffprobe
  count mismatches, encoded frame-rate mismatch, invalid downgrade counters,
  non-native or wrong-count surfaces/encode frames, non-direct surface
  handoff, non-zero software/pipe fallback, surface copies, CPU readback and
  encoder staging copies. A positive downgrade counter is reported but is not
  independently treated as CPU fallback: DirectYUV can be rejected while
  FullGraph Vulkan/NVENC remains native.
- Test coverage runs success and injected-failure fixtures through the actual
  shell runner, verifies missing-composition preflight records all 17 rows and
  launches zero renders, and separately locks manifest shape and criteria.

## Verification results

| Check | Result | Evidence |
|---|---|---|
| Shell syntax (`bash -n`) | PASS | completed 2026-10-30 after final change |
| Embedded runner Python syntax (`py_compile`) | PASS | extracted from shell heredoc after final change |
| `python3 tests/test_gpu_corpus_manifest.py` | PASS | synthetic success/failure scenarios including 60/30 fps mismatch, inconsistent prewarm/steady partition, plus 17-case no-render preflight |
| JSON parse and `git diff --check` on all task files | PASS | no whitespace errors |
| CLI `render --help` option contract | PASS | all runner options are recognized |
| 17-case scene preflight | BLOCKED as expected; 17/17 unregistered, zero rendered | final report `.tmp/gpu-baseline-current-preflight-final/gpu-baseline.json`; exit 1; `case_gate_pass=false`, `certification_eligible=false` |
| Built-in `DarkGridBackground`, 2-frame diagnostic | FAIL (not corpus certification) | `.tmp/gpu-baseline-current-b00-smoke/B00/render.log` and sidecar; renderer returned `VIDEO_ENCODER_FAILED`, with FFprobe rate 60 fps vs requested 30 fps; `execution_downgrade_count=1`, native surfaces/encodes 1/2, attribution 99.6323%, reread count 0 |
| Built-in `DarkGridBackground`, 120-frame diagnostic | FAIL (not corpus certification) | `.tmp/gpu-baseline-current-b00-120-smoke/gpu-baseline.json` rejects 1/120 native surfaces and 1/120 native encodes; direct `ffprobe -count_frames` reports 120 encoded frames at `r_frame_rate=30/1` and `avg_frame_rate=30/1`; attribution 98.4976%, reread count 0 |
| Encoded frame-rate contract | PASS in synthetic tests / rejected when deliberately altered | `encoded_fps_mismatch` fixture sets FFprobe output to 60/1 against requested 30/1; runner returns non-zero |

Neither render is treated as a benchmark PASS. The 2-frame run has a verified
container frame-rate mismatch. The 120-frame run has a valid 120-frame, 30 fps
container, but its sidecar reports only one native surface/encode frame out of
120. The strict native-frame contract therefore correctly rejects this
built-in composition. No claim is made that B00 passes.

## Exact corpus preflight command

```bash
CHRONON3D_ALLOW_DIRTY=1 CHRONON3D_CONTENT_PACK_ID=diagnostic-no-content-pack \
bash tools/benchmark_gpu_baseline.sh \
  --cli "$PWD/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli" \
  --out "$PWD/.tmp/gpu-baseline-current-preflight" \
  --corpus docs/benchmarks/gpu-corpus-v1.json \
  --backend vulkan --hardware nvenc --encoder-backend native \
  --gpu-hot-path-mode require_gpu_native --assets-root "$PWD"
```

Exit status: 1. Report: 17 rows, 17 `composition_not_registered`,
`case_gate_pass=false`, `certification_eligible=false`.

## Remaining external / pipeline blockers

1. Supply and identify the canonical content pack that registers B00–B11 and
   GPU_TEXT_01–05; rerun from a clean source checkout with its immutable pack
   revision/hash.
2. Investigate why the built-in FullGraph Vulkan/native NVENC render reports
   only 1 native surface/encode frame for 120 correctly encoded 30 fps frames,
   and why the 2-frame diagnostic finalizes at 60 fps instead of 30 fps; rerun
   both affected smoke cases after the pipeline fix.
3. Run all 17 exact manifest workloads (including B09 at 18,000 frames),
   preserve timing sidecars and encoded outputs, and require every declared
   gate to pass before recording corpus certification.
