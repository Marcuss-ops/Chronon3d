# B2 re-measurement — 2026-10-30

## Command

```bash
cd Chronon3d
python3 docs/evidence/100x-perf-waves-b2-20261029/run_b2_benchmark.py \
  build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli verify-20261030
```

The fixture is the canonical 456-frame static-title-drift plan, strict
Vulkan/NVENC (`--backend vulkan --gpu-hot-path-mode require_gpu_native
--hardware nvenc --encoder-backend native`). One run was executed after the
30-10 closeout measurements; the output SHA matches the historical fixture.

## Result

| Metric | Value | Gate | Outcome |
|---|---:|---:|---|
| Encoded frames | 456/456 | 456 | PASS |
| `render_loop_fps` | **240.03** | ≥300 | **FAIL** |
| `end_to_end_fps` | 42.63 | — | diagnostic |
| `frame_batch_drain_wait_us` | 12 | ≤50 | PASS |
| `nvenc_utilization_avg` (sidecar) | **77.78%** | ≥90% | **FAIL** |
| `nvidia-smi` non-zero samples | 100% | — | non-canonical |
| Output SHA-256 | `2520fabea7d548ee04b6f43bd747f9ad64cf1fc7aef1818d01a9f83cb1f7120c` | — | deterministic |

Per-pass device time: `composite` 175.261 ms + `layer_batch` 63.411 ms.
Per-frame medians: `composite` 0.384 ms + `layer_batch` 0.134 ms. The
composite pass count is 3,192 (7 per frame) and the layer batch count is 456
(1 per frame), so the ~240 fps ceiling is not a drain-wait problem; it is the
per-frame composite/layer work and the CPU-side submission structure.

## Decision

B2 remains **FAIL**. The 240 fps sample is inside the historical 232–252
band and confirms the ticket's 238.2/238.7 measurements; it does not change
the gate. The 100 overlay/min path must reduce per-frame work (active-item
culling, base-surface reuse, fewer composite passes) before the engine can
support the 100-item mix. No code change is claimed by this evidence file.

## Artifacts

- `docs/evidence/100x-perf-waves-b2-20261029/b2-verify-20261030-results.json`
- `docs/evidence/100x-perf-waves-b2-20261029/b2-verify-20261030-runs.json`
- `docs/evidence/100x-perf-waves-b2-20261029/static-text-456f-verify-20261030-nvenc.mp4`
- `docs/evidence/100x-perf-waves-b2-20261029/static-text-456f-verify-20261030-nvenc.log`
