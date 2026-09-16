# Chronon GPU measurements for Mike Tyson overlays — 2026-09-16

## Runtime workload

Source: RenderingGen autonomous-NLP runtime job `job_1789578693327275187_42435a63` (v4, SUCCEEDED), on NVIDIA RTX A4000, Chronon commit `a04e905a4a71a29e4ee61c92e606a442e48da7fa`.

- 23 overlay MP4s, 2,376 total frames, 1920×1080, 24 fps, Vulkan + native NVENC.
- All rendered items completed. Native GPU encode frames: 2,376; software fallbacks: 0.
- GPU readback: 0 bytes; CPU pixel readback: 0 bytes; CUDA host upload: 0 bytes.
- FullGraph RGBA→NV12 conversion: 2,376 frames; GPU resource upload: 440,477,260 bytes.
- Aggregate Chronon render-loop time: 4,759.7 ms (median 2.095 ms/frame across item reports).
- Aggregate per-item prepare time: 16,451.5 ms; backend initialization: 4,882.5 ms; encoder finalization: 2,324.0 ms.

The per-item preparation and backend setup dominate this workload's Chronon time. The GPU frame loop is already substantially faster than real time; the evidence points to amortizing repeated setup across overlay items as the useful next optimization, rather than changing the native pixel path.

## Release benchmark

Official `tools/benchmark_gpu_baseline.sh`, three 120-frame repetitions per profile, `DarkGridBackground`, Vulkan, native NVENC, `require_gpu_native`, 10 warmup framebuffers. All six renders passed on `full_graph_native`; no CPU fallback was permitted. Reports are in the ignored `.tmp/mike-tyson-gpu-baseline-20260916-{default,p1}/` directories.

| Encode profile | Median steady frame | Median throughput | Median process wall |
|---|---:|---:|---:|
| default | 4.651 ms | 215 fps | 2,303 ms |
| `p1` | 2.057 ms | 486 fps | 1,765 ms |

`p1` reduced median steady-frame time by 55.8% and median process wall by 23.4% in this benchmark. It is an explicit speed/quality tradeoff; no visual-quality comparison was run, so the production quality profile remains the default. Use `--encode-preset p1` only for a throughput-oriented preview/export profile after visual review.

## Scope

No Chronon source or default was changed, and no automated test suite was run. This measurement establishes the current GPU baseline and isolates the optimization target: repeated per-item preparation/backend setup. The benchmark outputs are local ignored artifacts and are not committed.

## Warm-cache runtime capture — v25

Source: PipelineGen job `job_1789586291389766129_29f0d51e` (SUCCEEDED), with 21 overlay outputs and 2,204 total frames at 1920×1080, 24 fps, Vulkan + native NVENC.

- Native GPU encode: 2,204 frames; software encode and fallback nodes: 0.
- GPU readback and CPU pixel readback: 0 bytes; GPU resource upload: 346,969,228 bytes.
- Aggregate render-loop time: 7,197.0 ms. Mean reported per-item steady average: 3.568 ms; mean per-item p95: 2.319 ms; maximum per-item p95: 3.477 ms, against a 41.667 ms frame budget.
- The per-item summaries reported 20 over-budget frames across 2,204 frames (0.91%).
- Aggregate preparation: 16,837.7 ms; backend initialization: 4,562.1 ms; encoder finalization: 3,376.5 ms; summed per-item total: 43,509.5 ms.

Preparation and backend initialization still cost more than the render loop, so amortizing setup across overlay items remains the measured optimization target. This run changed PipelineGen's entity-image reuse path; it did not change Chronon's render path or encode profile. New image uploads fell from 32 in the preceding warm-cache run to 1 while all 21 overlay renders completed.

No automated test suite was run. The evidence includes a successful PipelineGen build and this live runtime render.
