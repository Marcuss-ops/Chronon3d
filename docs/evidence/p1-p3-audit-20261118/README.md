# P1 + P3 audit verification — 2026-11-18

**Scope.** Fase 5 performance audit: P1 (NVDEC→CUDA→Vulkan per-surface idle
wait instead of a full-ring drain) and P3 (deferred surface-release overflow
retiring the oldest in-flight slot instead of `wait_for_pending()`).

**Code under test.** `Chronon3d` HEAD `ff949336a` (`perf: improve Vulkan render
paths`). The P1/P3 implementations landed earlier in `18077e3ae`
(`perf(render): cut per-frame Vulkan and encoder overhead`); this verification
rebuilt the current HEAD and re-ran every acceptance gate. No production code
was changed in this pass. The only source change is two regression tests in
`tests/backends/test_vulkan_submission_ring.cpp`.

**Build.** `build/chronon/linux-video-test`, Vulkan ON, CUDA interop ON,
`-j12` (then `-j8` for the final links), `ccache` + `mold`. Host: RTX A4000
16 GiB, 24 cores, 29 GiB RAM. `MemAvailable` stayed above 20 GiB before the
build; no OOM.

All counters below are read from `job.gpu` in the schema-v3 sidecar;
`job.surface_handoff_path` lives one level up at `job`.

## P1 — per-surface idle wait

Command:

```bash
chronon3d_cli render \
  --plan .tmp/p1p3_audit/nvdec_overlay_120.plan.json \
  --frames 0-119 --assets-root "$PWD" \
  --backend vulkan --hardware nvenc --encoder-backend native \
  --gpu-hot-path-mode require_gpu_native --report \
  -o .tmp/p1p3_audit/nvdec_overlay_120.mp4
```

The plan is a 1920×1080/30 NVDEC source (`tools/fixtures/calibration-source-1080p.mp4`,
144 frames) plus an animated text overlay, 120 frames.

| Counter | Measured |
|---|---:|
| `decoder_backend` | `nvdec` |
| `gpu_native_surface_frames` / `gpu_native_encode_frames` | 120 / 120 |
| `frame_batch_drain_wait_count` | **1** |
| `frame_batch_drain_wait_us` | **15 µs** |
| `frame_slot_wait_count` | 113 |
| `frame_slot_wait_us` | 180 677 µs |
| `overflow_wait_count` | 0 |
| `deferred_release_drain_wait_us` | 691 µs |
| `pending_release_high_water` | 2 |
| `gpu_submissions` | 122 |

Historical context: pre-patch sidecars in
`tests/motion_showcase/artifacts/05_puppet_pin_character/output.mp4.timing.json`
show `frame_batch_drain_wait_count=242` and `frame_slot_wait_count=0` — the
old “drain all six slots” signature. The workload differs, so this is context,
not a same-workload A/B (the patch is already in HEAD; a baseline would need a
rebuild of `588f2c14f`).

Gate results:

| Gate | Result |
|---|---|
| `chronon3d_vulkan_submission_ring_tests` | **10/10 cases, 3 313 assertions PASS** (includes the new per-surface test and the deferred-overflow test) |
| `chronon3d_native_decoder_teardown_tests` | **21/21 cases, 2 327 assertions PASS** |
| `chronon3d_backend_registry_tests` | **162/162 cases PASS** in 14 fresh processes (≤12 cases each); the single-process run still hits the known NVIDIA ICD `vkCreateInstance -9` limit after 26 instances |
| `tools/verify_zero_copy_end_to_end.sh --plan tools/... --direct-yuv` | **ZERO_COPY_PASS, 20/20 gates** (NVDEC→NVENC DirectYUV; host upload/readback 0, `surface_handoff_path=direct`, 60 native surface/encode frames) |

The FullGraph (NVDEC→CUDA→Vulkan RGBA) lane intentionally reports
`nv12_to_rgba_frames=120`; the zero-copy certificate is the DirectYUV lane,
which is the lane the script certifies.

## P3 — deferred-release overflow

Command:

```bash
chronon3d_cli render \
  --plan .tmp/p1p3_audit/p3_dof_effects_60.plan.json \
  --frames 0-59 --assets-root "$PWD" \
  --backend vulkan --hardware nvenc --encoder-backend native \
  --gpu-hot-path-mode require_gpu_native --report \
  -o .tmp/p1p3_audit/p3_dof_effects_60_anim.mp4
```

The plan is a 1920×1080/30 five-layer scene with camera DoF, four image
layers at different depths, glow and drop shadow. The title is animated so the
renderer does not collapse all frames into one duplicate (the first, static
run produced only 1 native frame; the animated run produces 60).

| Counter | Measured |
|---|---:|
| `frame_batch_drain_wait_count` | **0** |
| `frame_batch_drain_wait_us` | **0 µs** |
| `frame_slot_wait_count` | 26 |
| `overflow_wait_count` | **30** |
| `pending_release_high_water` | **52** (> cap 48) |
| `deferred_release_drain_wait_us` | 970 674 µs cumulative |
| `deferred_release_flush_count` | 60 |
| `gpu_submissions` | 66 |
| `gpu_native_surface_frames` / `gpu_native_encode_frames` | 60 / 60 |

The overflow cap is `8 * kSlotCount = 48`. The run crossed it and used the
oldest-slot path 30 times while **never** invoking the full-ring drain
(`frame_batch_drain_wait_count=0`). `deferred_release_drain_wait_us` is the
cumulative duration of 60 flushes (it includes the 30 one-slot waits), not a
single stall.

Gate results:

| Gate | Result |
|---|---|
| `chronon3d_backend_registry_tests` | 162/162 cases PASS (same chunked run as P1) |
| `chronon3d_render_graph_tests` | **PASS** via CTest, 467 cases, 92.45 s |
| `chronon3d_effects_tests` | **PASS** via CTest, 335 cases, 41.18 s |

CTest sets `Working Directory=Chronon3d`; running the binaries directly from
`build/.../tests` gives false text/glow failures because font and asset paths
are resolved from the repository root.

## Artifacts

- `nvdec_overlay_120.mp4.timing.json` — authoritative P1 sidecar.
- `p3_dof_effects_60_anim.mp4.timing.json` — authoritative P3 sidecar.
- `p1_render.log`, `p3_render_anim.log` — full render logs (no Vulkan/CUDA
  validation errors).
- `zero_copy_report_direct_yuv.json`, `zero_copy_direct_yuv.log` — 20/20 gate
  report.
- `registry_chunks.log` — all 162 registry cases across chunked processes.
- `ctest_effects_render_graph.log` — CTest summary for the two P3 suites.

## Verdict

- **P1: PASS.** Per-surface idle wait removes the periodic full-ring drain;
  120-frame NVDEC export shows one 15 µs full drain and 113 per-slot waits.
- **P3: PASS.** Overflow retires one oldest in-flight slot; the 60-frame
  DoF/effects run crossed the cap 30 times with zero full-ring drains.

Limits: no same-workload pre-patch A/B was built (the patch is already in
HEAD), and the registry suite still needs fresh processes because of the
NVIDIA ICD instance limit. Both are recorded here rather than promoted to a
green same-SHA claim.
