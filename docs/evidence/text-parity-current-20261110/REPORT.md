# Text CPU/Vulkan parity — current checkout, 2026-11-10

**Result: FAIL, 0/8 corpus cases pass all frozen thresholds.** This is a fresh
same-SHA run from the current checkout, kept separate from the prior reports.
The release CLI SHA-256 is
`fe39acb857280be500fc8c8149f014687e683ccf7c5b7dc8f9c33a4247b9582d`.
The command was:

```bash
cmake --build build/chronon/linux-video-release -j14
python3 tools/verify_text_cpu_gpu_parity.py \
  --corpus tests/text/fixtures/pixel_parity/manifest.json \
  --out docs/evidence/text-parity-current-20261110 \
  --require-same-sha --repeat
```

The build reported `ninja: no work to do` (exit 0). The verifier completed all
8 cases, enforced native MTSDF Vulkan routing, and checked identical software
repeat output. It exited 1 because every case misses at least one frozen
threshold. Per-frame metrics, route logs, timing files, raw captures, repeat
captures and diffs are in this directory; `report.json` is authoritative.

| Case | alpha IoU | Coverage | RGB MAE | RGB p99 | SSIM | PSNR dB |
|---|---:|---:|---:|---:|---:|---:|
| `latin_basic` | 0.947735 | 1.000000 | 0.020837 | 0.517647 | 0.982458 | 21.028 |
| `exact_3d` | 0.966466 | 0.966466 | 0.002091 | 0.039216 | 0.999649 | 44.307 |
| `effects` | 0.921818 | 0.921818 | 0.017320 | 0.164706 | 0.995647 | 28.887 |
| `multi_shadow` | 0.932386 | 0.934228 | 0.087519 | 0.435294 | 0.973515 | 20.880 |
| `rtl_caret` | 0.916726 | 0.964025 | 0.006228 | 0.192157 | 0.970406 | 24.348 |
| `transitions` | 0.975236 | 1.000713 | 0.007663 | 0.196078 | 0.997000 | 28.750 |
| `fractional` | 0.985373 | 0.985373 | 0.001062 | 0.027451 | 0.999823 | 43.079 |
| `multipage` | 0.978911 | 0.992217 | 0.005623 | 0.250980 | 0.994018 | 26.095 |

Thresholds are unchanged: IoU ≥ 0.995, coverage 0.99–1.01, MAE ≤ 2/255,
p99 ≤ 8/255, SSIM ≥ 0.98, PSNR ≥ 35 dB, plus bbox and flat-region checks.
This run reproduces the prior same-SHA result; it does not certify closure.
