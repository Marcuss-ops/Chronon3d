# Text CPU/Vulkan parity — after typed cache migration, 2026-11-10

**Result: FAIL, 0/8 corpus cases pass all frozen thresholds.** The release CLI
SHA-256 is
`74d9b861e7509e81ef271dda96294f896c93989c1c7824f77e83611687b2cf22`.
The run was captured after the `CompiledComposition` cache serializer and
FlatBuffers reader were removed. The command was:

```bash
python3 tools/verify_text_cpu_gpu_parity.py \
  --corpus tests/text/fixtures/pixel_parity/manifest.json \
  --out docs/evidence/text-parity-current-cache-20261110 \
  --require-same-sha --repeat
```

The verifier rendered all 8 cases with native Vulkan MTSDF routing and
byte-identical software repeats. It exited 1 because all cases miss at least
one unchanged acceptance threshold. The result reproduces the previous
same-SHA metrics exactly; the cache migration does not touch text rendering.
The per-frame JSON, raw captures, route logs, timings and diffs are preserved
in this directory.

| Case | IoU | Coverage | MAE | p99 | SSIM | PSNR dB |
|---|---:|---:|---:|---:|---:|---:|
| `latin_basic` | 0.947735 | 1.000000 | 0.020837 | 0.517647 | 0.982458 | 21.028 |
| `exact_3d` | 0.966466 | 0.966466 | 0.002091 | 0.039216 | 0.999649 | 44.307 |
| `effects` | 0.921818 | 0.921818 | 0.017320 | 0.164706 | 0.995647 | 28.887 |
| `multi_shadow` | 0.932386 | 0.934228 | 0.087519 | 0.435294 | 0.973515 | 20.880 |
| `rtl_caret` | 0.916726 | 0.964025 | 0.006228 | 0.192157 | 0.970406 | 24.348 |
| `transitions` | 0.975236 | 1.000713 | 0.007663 | 0.196078 | 0.997000 | 28.750 |
| `fractional` | 0.985373 | 0.985373 | 0.001062 | 0.027451 | 0.999823 | 43.079 |
| `multipage` | 0.978911 | 0.992217 | 0.005623 | 0.250980 | 0.994018 | 26.095 |

Limits remain IoU ≥ 0.995, coverage 0.99–1.01, MAE ≤ 2/255, p99 ≤ 8/255,
SSIM ≥ 0.98, PSNR ≥ 35 dB, plus the frozen bbox and flat-region checks.
