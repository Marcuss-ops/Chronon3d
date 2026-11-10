# MTSDF B-spline reconstruction probe — `latin_basic`

**Result: rejected.** This probe wired the existing `atlas_alpha_smooth()`
filter into MTSDF fill reconstruction for the frozen `latin_basic` case only.
It did not change the corpus, metric definitions or thresholds. The candidate
CLI SHA-256 was
`85d84a72c77a4f994c285f257ae459a9f2dd94783a834dd49f9d50d53223b72b`; its
software repeat was byte-identical and Vulkan used the native MTSDF route.

All three sampled frames produced identical metrics:

| Metric | Bilinear baseline | B-spline candidate | Gate |
|---|---:|---:|---:|
| alpha IoU | 0.947735 | 0.947630 | ≥ 0.995 |
| coverage | 1.000000 | 0.999693 | 0.99–1.01 |
| RGB MAE | 0.020837 | 0.020722 | ≤ 2/255 |
| RGB p99 | 0.517647 | 0.513726 | ≤ 8/255 |
| SSIM | 0.982458 | 0.982544 | ≥ 0.98 |
| PSNR | 21.028 dB | 21.050 dB | ≥ 35 dB |

The tiny MAE/PSNR gains do not compensate for the slightly worse IoU; both
versions fail the frozen pixel gate. The experimental call was removed and the
existing bilinear MTSDF path restored. `report.json` and the case directory
retain the full targeted run, raw files, route logs, repeat and diffs.
