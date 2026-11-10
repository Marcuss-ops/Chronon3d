# Text CPU/Vulkan parity — High range 40, same-SHA rerun

**Result: FAIL.** The frozen eight-plan corpus ran against one CLI SHA with strict native Vulkan (`require_gpu_native`) and a software repeat. Software repeat raws are byte-identical for all cases; all Vulkan routes completed. All eight cases still miss one or more unchanged thresholds.

CLI SHA-256: `a7103d514f2acf74fcbd71a187bf22bf09e1a19dfc641c7ba79177c3bfd241f7`.

| Case | alpha IoU | Coverage | RGB MAE | RGB p99 | SSIM | PSNR dB | Software repeat |
|---|---:|---:|---:|---:|---:|---:|---|
| `latin_basic` | 0.94774 | 1.00000 | 0.02084 | 0.51765 | 0.98246 | 21.03 | identical |
| `exact_3d` | 0.96647 | 0.96647 | 0.00209 | 0.03922 | 0.99965 | 44.31 | identical |
| `effects` | 0.92182 | 0.92182 | 0.01732 | 0.16471 | 0.99565 | 28.89 | identical |
| `multi_shadow` | 0.93239 | 0.93423 | 0.08752 | 0.43529 | 0.97351 | 20.88 | identical |
| `rtl_caret` | 0.91673 | 0.96403 | 0.00623 | 0.19216 | 0.97041 | 24.35 | identical |
| `transitions` | 0.97524 | 1.00071 | 0.00766 | 0.19608 | 0.99700 | 28.75 | identical |
| `fractional` | 0.98537 | 0.98537 | 0.00106 | 0.02745 | 0.99982 | 43.08 | identical |
| `multipage` | 0.97891 | 0.99222 | 0.00562 | 0.25098 | 0.99402 | 26.09 | identical |

Thresholds remain unchanged: alpha IoU ≥ 0.995, coverage 0.99–1.01, RGB MAE ≤ 2/255, RGB p99 ≤ 8/255, SSIM ≥ 0.98 and PSNR ≥ 35 dB, plus bbox and flat-background checks. `report.json` contains per-frame metrics, failures, hashes, and Vulkan identity. Case directories retain captures, timings, logs, repeats, and image diffs.

The range40 profile shows a small improvement on the focused shadow/effects cases versus range32, but it does not close any corpus case. The stricter pixel gate remains open; this profile run is evidence for the candidate only, not a parity claim.

## Focused diagnosis: `latin_basic`, frame 0

The software and Vulkan alpha masks both contain 19,565 pixels, and their
bounding boxes are `(334,365)-(946,435)` and `(333,365)-(946,435)`. Their IoU
is 0.94774. A diagnostic one-pixel translation lowers IoU (best tested offset:
zero), so a global run-placement correction does not explain the mismatch.
The amplified diff is concentrated on glyph contours. This points to coverage
reconstruction/antialiasing as the next area to investigate; it is an inference
from the mask and diff evidence, not a proven implementation root cause.
