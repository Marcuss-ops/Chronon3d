# Text pixel coverage experiment — 2×2 MTSDF samples

**Result: REJECTED.** The experiment sampled four MTSDF field positions per output pixel and averaged the coverage ramp. The frozen `latin_basic` case remains FAIL. CLI SHA-256: `2001a38c3d28db925154046a5ae6598881b85a46522bb0f14dfc1be0fda9bc8c`. Software repeat identical: `True`.

| alpha IoU | Coverage | RGB MAE | RGB p99 | SSIM | PSNR dB |
|---:|---:|---:|---:|---:|---:|
| 0.946881 | 1.040020 | 0.021519 | 0.427451 | 0.983781 | 21.425 |

Against the range40 single-sample baseline (IoU 0.947735, coverage 1.000000, MAE 0.020837, p99 0.517647), 2×2 sampling worsened IoU and over-covered the mask by 4%. It is not kept in the source. This rejects simple subpixel averaging as a fix; the next renderer change needs a coverage representation that matches the FreeType reference more directly.
