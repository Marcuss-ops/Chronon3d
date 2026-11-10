# Text CPU/Vulkan parity — same-SHA rerun, 2026-11-09

**Result: FAIL.** This report reruns the frozen corpus on the current release
CLI after the 128-em High profile was restored. Binary SHA-256:
`92247be1bfe18ccc50a5021e48641cedf9757cc281368d7c20e584acb58b88f4`.
The verifier ran software twice and Vulkan with
`--gpu-hot-path-mode require_gpu_native`, rejecting native-route fallback.
Software repeat raw output is byte-identical for every case. All 8 Vulkan
cases produce `route=mtsdf`, but each misses one or more frozen thresholds.

| Case | alpha IoU | Coverage | RGB MAE | RGB p99 | SSIM | PSNR dB |
|---|---:|---:|---:|---:|---:|---:|
| `latin_basic` | 0.94809 | 1.00046 | 0.02060 | 0.50980 | 0.98263 | 21.07 |
| `exact_3d` | 0.96609 | 0.96767 | 0.00213 | 0.03922 | 0.99964 | 44.21 |
| `effects` | 0.84488 | 0.84488 | 0.02177 | 0.24706 | 0.99194 | 26.41 |
| `multi_shadow` | 0.81995 | 0.81995 | 0.08563 | 0.42353 | 0.97897 | 21.94 |
| `rtl_caret` | 0.90853 | 0.96616 | 0.00636 | 0.20000 | 0.97007 | 24.31 |
| `transitions` | 0.97563 | 1.00006 | 0.00725 | 0.17647 | 0.99723 | 29.10 |
| `fractional` | 0.98617 | 0.98802 | 0.00089 | 0.02353 | 0.99987 | 44.34 |
| `multipage` | 0.97914 | 0.99211 | 0.00563 | 0.25490 | 0.99425 | 26.27 |

Thresholds remain unchanged: alpha IoU ≥ 0.995, coverage 0.99–1.01, RGB MAE
≤ 2/255, RGB p99 ≤ 8/255, SSIM ≥ 0.98 and PSNR ≥ 35 dB, plus bbox and flat
background checks. See [`report.json`](report.json) for each sampled frame,
the device identity, plan hashes and failure reasons; case directories retain
raw frames, repeat outputs, timing, route logs and visual diffs.

The alpha/coverage mismatch dominates `effects`, `multi_shadow` and
`rtl_caret`; `exact_3d` and `fractional` have strong SSIM/PSNR but still fail
the mask/coverage criteria. No AA exception or threshold relaxation is used.
