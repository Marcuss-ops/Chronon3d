# Text CPU/Vulkan parity — contour subdivision candidate

**Result: REJECTED.** This full frozen corpus run evaluated matching the software contour flattener minimum segment counts (quadratic 2, cubic 3) for the MTSDF generator. CLI SHA-256: `e1e95417a3e90ab2f718ba008bd4cd10218f2dcd66267c4e80b3671678df1c8f`. Software repeats were identical in all cases; strict MTSDF Vulkan route completed. The pixel gate still fails 8/8.

| Case | IoU | Coverage | RGB MAE | p99 | SSIM | PSNR dB |
|---|---:|---:|---:|---:|---:|---:|
| `latin_basic` | 0.947735 | 1.000000 | 0.020837 | 0.517647 | 0.982458 | 21.028 |
| `exact_3d` | 0.966466 | 0.966466 | 0.002091 | 0.039216 | 0.999649 | 44.307 |
| `effects` | 0.921818 | 0.921818 | 0.017319 | 0.164706 | 0.995647 | 28.888 |
| `multi_shadow` | 0.932386 | 0.934228 | 0.087518 | 0.435294 | 0.973515 | 20.880 |
| `rtl_caret` | 0.916679 | 0.964077 | 0.006228 | 0.192157 | 0.970402 | 24.348 |
| `transitions` | 0.975236 | 1.000713 | 0.007663 | 0.196078 | 0.997000 | 28.750 |
| `fractional` | 0.985373 | 0.985373 | 0.001062 | 0.027451 | 0.999823 | 43.079 |
| `multipage` | 0.978911 | 0.992217 | 0.005623 | 0.250980 | 0.994018 | 26.095 |

Against the immediately preceding same-SHA baseline (`text-parity-20261109-final-sha`), every case is effectively unchanged. RTL IoU moved only 0.916726 → 0.916679; no case closed. The generation ID bump was therefore reverted with the code change. `report.json` and case artifacts retain the full run.
