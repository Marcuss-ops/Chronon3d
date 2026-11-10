# Text CPU/Vulkan parity — High range 32, same-SHA rerun

**Result: FAIL.** The run uses the frozen eight-plan corpus, production profile,
strict Vulkan native routing and a same-backend software repeat. CLI
SHA-256: `86581d06c6635f6caa613bfe0f7d586d55b436287c55d955e59b21f97f2329f9`.
Software repeats are byte-identical for all cases. Every Vulkan case reports
`route=mtsdf`; every case still misses at least one unchanged threshold.

| Case | alpha IoU | Coverage | RGB MAE | RGB p99 | SSIM | PSNR dB |
|---|---:|---:|---:|---:|---:|---:|
| `latin_basic` | 0.95306 | 0.99468 | 0.02015 | 0.51373 | 0.98274 | 21.10 |
| `exact_3d` | 0.96510 | 0.96707 | 0.00209 | 0.03922 | 0.99963 | 43.41 |
| `effects` | 0.90408 | 0.90408 | 0.01713 | 0.16078 | 0.99584 | 29.11 |
| `multi_shadow` | 0.93119 | 0.93289 | 0.08753 | 0.43529 | 0.97362 | 20.89 |
| `rtl_caret` | 0.91262 | 0.97038 | 0.00630 | 0.20392 | 0.97032 | 24.34 |
| `transitions` | 0.98036 | 0.99554 | 0.00717 | 0.18431 | 0.99720 | 29.05 |
| `fractional` | 0.98566 | 0.98878 | 0.00105 | 0.03137 | 0.99982 | 43.02 |
| `multipage` | 0.98164 | 0.99246 | 0.00547 | 0.23529 | 0.99446 | 26.43 |

Compared with the 128-em/range-16 run, scaling range/padding to 32/48 improves
the worst `effects` IoU from 0.84488 to 0.90408 and `multi_shadow` from 0.81995
to 0.93119. `exact_3d` changes from 0.96609 to 0.96510 at its worst frame, so
this profile is not a universal parity fix. The focused comparison and retained
raws are in [`range32-experiment`](../text-parity-20261109-hq/range32-experiment/REPORT.md).

Thresholds are unchanged: alpha IoU ≥ 0.995, coverage 0.99–1.01, RGB MAE
≤ 2/255, RGB p99 ≤ 8/255, SSIM ≥ 0.98 and PSNR ≥ 35 dB, plus bbox/flat
background checks. [`report.json`](report.json) contains per-frame failures,
plan and binary hashes, Vulkan identity and route evidence. Case directories
retain raw frames, repeat output, timings, route logs and visual diffs.
