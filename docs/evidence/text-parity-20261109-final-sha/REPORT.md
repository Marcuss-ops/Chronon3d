# Text CPU/Vulkan parity — final current-binary rerun

**Result: FAIL.** This same-SHA rerun is against the current restored source and binary, with strict native Vulkan routing and a software repeat. Software repeat raws are identical for all eight cases. All eight cases still fail at least one frozen threshold.

CLI SHA-256: `fe39acb857280be500fc8c8149f014687e683ccf7c5b7dc8f9c33a4247b9582d`.

| Case | alpha IoU | Coverage | RGB MAE | RGB p99 | SSIM | PSNR dB | Repeat |
|---|---:|---:|---:|---:|---:|---:|---|
| `latin_basic` | 0.94774 | 1.00000 | 0.02084 | 0.51765 | 0.98246 | 21.03 | identical |
| `exact_3d` | 0.96647 | 0.96647 | 0.00209 | 0.03922 | 0.99965 | 44.31 | identical |
| `effects` | 0.92182 | 0.92182 | 0.01732 | 0.16471 | 0.99565 | 28.89 | identical |
| `multi_shadow` | 0.93239 | 0.93423 | 0.08752 | 0.43529 | 0.97351 | 20.88 | identical |
| `rtl_caret` | 0.91673 | 0.96403 | 0.00623 | 0.19216 | 0.97041 | 24.35 | identical |
| `transitions` | 0.97524 | 1.00071 | 0.00766 | 0.19608 | 0.99700 | 28.75 | identical |
| `fractional` | 0.98537 | 0.98537 | 0.00106 | 0.02745 | 0.99982 | 43.08 | identical |
| `multipage` | 0.97891 | 0.99222 | 0.00562 | 0.25098 | 0.99402 | 26.09 | identical |

The frozen limits are unchanged: IoU ≥ 0.995; coverage 0.99–1.01; RGB MAE ≤ 2/255; p99 ≤ 8/255; SSIM ≥ 0.98; PSNR ≥ 35 dB; bbox/flat checks. `report.json` has per-frame failures and Vulkan identity; case folders contain raw captures, repeat data, route logs, timings and visual diffs.
