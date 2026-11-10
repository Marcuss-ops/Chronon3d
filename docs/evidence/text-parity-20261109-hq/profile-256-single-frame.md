# MTSDF 256-em experiment — 2026-11-09

This is a one-frame diagnostic for the HQ profile; it is not a corpus pass.
The frozen `latin_basic` frame 0 plan was copied to `/tmp`, with composition
and layer duration both set to one frame. Rendering used the same production
profile and raw RGBA sink as the T0-01 report. Vulkan ran with
`--gpu-hot-path-mode require_gpu_native` and logged `route=mtsdf`.

The temporary build used a 256-em profile with `generation_id=3`. The measured
values were:

| Metric | Result | T0-01 threshold |
|---|---:|---:|
| Alpha IoU | 0.947848 | ≥ 0.995 |
| Coverage ratio | 1.000613 | 0.99–1.01 |
| RGB MAE | 0.020379 | ≤ 2/255 |
| RGB p99 | 0.513726 | ≤ 8/255 |
| SSIM | 0.982743 | ≥ 0.98 |
| PSNR | 21.099 dB | ≥ 35 dB |
| Bbox | software `[334,365,946,435]`; Vulkan `[333,365,946,435]` | ±1 px |

The run still fails the frozen parity gate. Compared with the existing 128-em
HQ corpus's worst `latin_basic` alpha IoU (0.948086), 256 em did not improve
alpha parity. No threshold was changed. The experiment was discarded and the
source profile restored to 128 em / `generation_id=2`.

Experiment CLI SHA-256:
`b6a1badabb2c942aab4e70def534557a3da44efa8217875c34dd2a8ec5ad1770`.

Raw artifact SHA-256 values:

- Software: `de6efaa60ac2799a79a62209ca019ed5545cadc146ae60d5b288b0328972ebc9`
- Vulkan: `382131e1a8daaa7a771931b082328a26a5c6636b217e4a96e3ac4728ef161364`

The temporary plan, both raw files and their timing sidecars are retained in
[`profile-256/`](profile-256/). The rerun corpus at
[`../text-parity-20261109-rerun/REPORT.md`](../text-parity-20261109-rerun/REPORT.md)
is the retained multi-frame evidence for the restored 128-em profile.
