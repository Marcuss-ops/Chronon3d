# Closeout 2026-10-30 — F1, T05, F2, AAC, B2

Binary under test:

- `Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli`
- SHA-256 `67b8c1d5ed32160b4d33a32d1e996ecfe20d981690a407da0802a2f12e6212d4`
- Source-identical rebuild after reverting the B2 depth/surface experiments.
- Host: Linux, NVIDIA RTX A4000 16 GB, Vulkan strict-native, NVENC.

All commands below run from `Chronon3d/`.

| Item | State | Key result |
|---|---|---|
| F1 — native NVENC staging | **CLOSED (local)** | NV12 staging matches the CUDA frames context; mean RGB 53.5169, no half-plane inversion |
| T05 — lights/shadows black residue | **IMAGE GATE PASS** | 5/5 caster-off + 5/5 caster-on, gate 30/30 PASS; quality review and same-SHA CI open |
| F2 — DoF lane | **CLOSED as lane choice** | 853.24 vs 152.30 ms/frame (5.60×), pixel parity MAE 0.4424 / p99 3 |
| AAC — audio-aware chunk parent | **PASS (fixture-scoped)** | `TestAudioAwareChunkParentMatchesSingleJob` PASS in 7.772 s |
| B2 — 456-frame throughput | **FAIL** | 238.200 render-loop fps, 456/456, still 20.60% below the 300 fps gate |

---

## 1. F1 — native NVENC staging pixel format (closed)

**Root cause.** The software-backend path allocates a host staging `AVFrame`
and uploads it with `av_hwframe_transfer_data` into a CUDA frames context whose
`sw_format` is NV12. The staging frame was declared `AV_PIX_FMT_YUV420P`, so the
transfer copied the planar U and V planes into one interleaved UV plane. The
decoded native output showed the classic half-pink / half-green split.

**Fix.** `apps/chronon3d_cli/utils/video/native_av_encoder_write.cpp`:

- the staging format is `AV_PIX_FMT_NV12` when `gpu_nvenc_ && !hdr_output`;
- `FramePlanes` fills `y` + interleaved `uv`, with `stride_uv =
  linesize[1]`, instead of `u`/`v`;
- HDR / 10-bit output keeps the planar `Yuv420P10` path.

**Verification.** T05 production plan, frame 0, software backend with native
NVENC:

- decoded mean RGB **53.5169**, identical to the current T05 caster-on gate
  value; no plane inversion;
- `execution_path=full_graph_native`; render 216.7 ms;
- against the stored T05 RGB reference the residual is a range/encoding
  difference (MAE 1.5908, p99 95, RGB-L1 > 8 on 1.9545% of channels), not the
  old half-plane swap.

Artifacts: `f1/f1-native-new.mp4.timing.json`, `f1/f1-native-new.log`.

## 2. T05 — 5/5 caster-off + 5/5 caster-on (image gate)

Ten fresh strict Vulkan/native NVENC renders (five per mode, no probes) on the
final binary. The canonical `run_t05_acceptance.py` gate passed all 30 checks:

- caster-off mean RGB `53.1802`, zero black pixels, opaque ratio `1.0`;
- caster-on mean RGB `53.5169`, zero black pixels, opaque ratio `1.0`;
- every A/B pair changes `8.8485%` of pixels above RGB-L1 > 8;
- every render exited 0 and muxed one frame;
- decoded RGB SHA-256 is identical across the five replicas of each mode:
  off `bb1bd0615b95f5ffb37435a256725941748a9c63826bce068993108d4d14dcc1`,
  on `46c385500e7f2b0a5a542866c5a472ff453440e3cff19e8c0ff0e535287c6f8b`.

Artifacts: `t05/gate.txt`, ten `*.timing.json` and ten `*.log` files.

This closes the reproduced black residue for this plan on this binary. It does
not close the ticket: the production shadow-quality review and the same-SHA CI
certification remain open.

## 3. F2 — DoF lane (closed as a lane decision)

Same DoF-on frame set, two frames per lane:

| Lane | render | avg/frame | relative |
|---|---:|---:|---:|
| software | 1706.57 ms | 853.24 ms | 1.00× |
| Vulkan strict-native | 304.66 ms | 152.30 ms | **5.60×** |

Pixel parity between the two decoded RGB outputs (frame 0): MAE **0.4424**, p99 **3**,
RGB-L1 > 8 on **0.0084%** of channels, max 63. The native lane completed with no
software fallback. The software lane remains available but is no longer the
production DoF lane; the historical ~30 s/frame figure is not reproduced on
this plan.

Artifacts: `f2/dof-software.mp4.timing.json`, `f2/dof-vulkan.mp4.timing.json`,
`f2/dof-software.log`, `f2/dof-vulkan.log`.

## 4. AAC — audio-aware chunk parent (pass)

```
cd RenderingGen/renderinggen
CHRONON_BINARY=/.../Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli \
  go test ./internal/processor -run TestAudioAwareChunkParentMatchesSingleJob -count=1
```

Result: **PASS**, 7.772 s, on the same final binary. The test compares the
assembled parent against the single-job render on sealed output facts, duration,
360 decoded frames and the non-negative AAC packet hashes. Scope is the fixture
inside the test (sine 997 Hz, 12 s, AAC, two GOP-aligned children), not every
source format.

## 5. B2 — 456-frame throughput (still failing)

Canonical harness:

```
python3 docs/evidence/100x-perf-waves-b2-20261029/run_b2_benchmark.py \
  build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli final-20261030
```

Result on the final binary:

- exit 0, **456/456** frames encoded, zero duplicate-frame reuse;
- render-loop **238.200 fps**; end-to-end 139.284 fps; drain 12 µs/job;
- active-sample `nvidia-smi` encoder utilization 95.43%, canonical sidecar null;
- output SHA-256 `2520fabea7d548ee04b6f43bd747f9ad64cf1fc7aef1818d01a9f83cb1f7120c`.

That is **20.60% below 300 fps**: the gate remains NOT ACCEPTED.

Targeted variants did not move it: 16 CPU render threads 238.50 fps, 12-slot
depth 235.81, 6-slot depth 233.57, 16 NVENC surfaces 236.30. All are inside the
232–239 fps run-to-run band; the depth and surface changes were reverted, so the
final binary is the baseline depth-4 / 8-surface configuration. The remaining
cost is the ~2.5 ms `prepare` / `avcodec_send_frame` serialization, not queue
depth.

Artifacts: `../100x-perf-waves-b2-20261029/b2-final-20261030-runs.json`,
`../100x-perf-waves-b2-20261029/b2-final-20261030-results.json`, and the
encoded fixture `../100x-perf-waves-b2-20261029/static-text-456f-final-20261030-nvenc.mp4`.

## Reproduction notes

- T05: `docs/evidence/lights-shadows/T05-c3d-production-20261022/t05-production.plan.json`
  and `docs/evidence/lights-shadows/T05-native-vulkan-20261028/caster-off-control.plan.json`,
  frame 0, `--backend vulkan --gpu-hot-path-mode require_gpu_native --hardware
  nvenc --encoder-backend native`.
- F1: `t05-production.plan.json`, frame 0, `--backend software --encoder-backend
  native --hardware nvenc`.
- F2: `docs/evidence/camera-authority-migration/T16/dof-ablation/t16_three_depth_dof_on.plan.json`,
  software vs strict Vulkan/native NVENC.
- B2: the canonical command above; the harness samples `nvidia-smi` every 200 ms.
