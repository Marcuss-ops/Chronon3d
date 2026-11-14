# Text AE parity — T1-01/T1-02/T2-01 implementation update (2026-11-13)

**Status:** IMPLEMENTATIONS LANDED — frozen pixel corpus still **1/8 PASS**.
This is **not** a parity PASS and does **not** remove
[ADR-040](../../adr/ADR-040-text-parity-product-exception.md).

- **Build:** `cmake --build build/chronon/linux-video-release --target chronon3d_cli -j8`
  — exit 0.
- **CLI SHA-256:** `410d7395a2c8782695f4e43a69e91b4312088742f7f2679b81bccea5647d20d3`.
- **Corpus:** `tests/text/fixtures/pixel_parity/manifest.json`.
- **Verifier:** `tools/verify_text_cpu_gpu_parity.py` (per-case runs; `--repeat`
  on the passing `multipage` case).
- **Thresholds:** unchanged (IoU ≥ 0.995, coverage 0.99–1.01, RGB MAE ≤ 2/255,
  RGB p99 ≤ 8/255, SSIM luma ≥ 0.98, PSNR luma ≥ 35 dB, bbox slack 1 px,
  flat max 2/255).

## Frozen corpus result

All eight cases were rendered on the final binary. The table reports the worst
frame per case (the verifier's contract); `multipage` is the only PASS.

| Case | alpha IoU | coverage | RGB MAE | RGB p99 | SSIM luma | PSNR luma | Failures |
|---|---:|---:|---:|---:|---:|---:|---|
| `multipage` | 0.995177 | 1.000744 | 0.001270 | 0.019608 | 0.999260 | 35.162 | — **PASS** |
| `latin_basic` | 0.985900 | 0.998673 | 0.002934 | 0.082353 | 0.999306 | 35.058 | IoU, p99 |
| `transitions` | 0.983597 | 1.003218 | 0.003223 | 0.094118 | 0.999184 | 34.408 | IoU, p99, PSNR |
| `fractional` | 0.963981 | 0.985111 | 0.007253 | 0.176471 | 0.995927 | 29.418 | IoU, coverage, p99, PSNR |
| `exact_3d` | 0.943576 | 0.961831 | 0.012593 | 0.227451 | 0.990470 | 30.023 | IoU, coverage, bbox 2 px, p99, PSNR |
| `rtl_caret` | 0.928066 | 0.956654 | 0.004236 | 0.039216 | 0.975768 | 25.232 | IoU, coverage, p99, PSNR, SSIM |
| `multi_shadow` | 0.927669 | 0.933783 | 0.085461 | 0.431373 | 0.975264 | 21.176 | IoU, coverage, bbox 3 px, RGB MAE/p99, SSIM |
| `effects` | 0.923020 | 0.923020 | 0.019771 | 0.180392 | 0.995348 | 28.594 | IoU, coverage, bbox 5 px, RGB MAE/p99, flat background |

The residual is the historical Blend2D analytic coverage vs MTSDF
reconstruction gap, not a missing implementation gate. The seven failing cases
retain their pre-existing status and are not declared PASS.

`multipage` was also run with `--repeat`; `repeat_software_identical=true`.
The other seven cases short-circuit before the repeat stage because they already
fail the pixel comparison, so no repeat result is claimed for them.

## Targeted implementation gates

### T1-01 — GPU canonical union-mask shadow

`canonical.plan.json`, 3 frames:

- mask pixels: software `61219`, Vulkan `60891`;
- alpha IoU `0.994251` (threshold 0.995);
- coverage `0.994642`;
- RGB MAE `0.003806`, RGB p99 `0.027451`;
- flat background difference `2/255`.

The union-mask pass, separable blur and linear source-over now run on Vulkan.
The blur kernel was aligned to the CPU's full 3-sigma support
(`ceil(3 * sigma)`); before that change the Vulkan 2-sigma truncation shifted
the silhouette inward. The remaining `0.000749` IoU shortfall is edge
quantization of the canonical mask, so T1-01 is implemented but **not PASS**.

### T1-02 — GPU VectorStroke

`vector.plan.json`, 3 frames:

- mask pixels: software `36118`, Vulkan `37302`;
- alpha IoU `0.966888`;
- coverage `1.032781`;
- RGB MAE `0.010481`, RGB p99 `0.164706`;
- PSNR luma `31.798 dB`, SSIM luma `0.998076`.

The dedicated `CoverageRgba` atlas, Clipper2 join/miter expansion and the
per-glyph fill/stroke channels render correctly (fill is white, stroke is red).
The residual is outline/AA expansion against the Blend2D stroke, so T1-02 is
implemented but **not PASS**.

### T2-01 — transitions + spans / paint ranges

`t2_spans.plan.json`, 61 frames; all sampled frames have the same metrics:

- alpha IoU `0.983597`;
- coverage `1.003218`;
- RGB MAE `0.003092`, RGB p99 `0.070588`;
- PSNR luma `36.736 dB`, SSIM luma `0.999222`.

The decoder/compiler now validate keyframe spans and paint ranges against the
keyframe text, with keyframe ranges taking precedence over layer-level
fallbacks. The native per-glyph route now copies `GlyphInstanceState::fill`
into the dynamic record, so the blue span is visible instead of the run's white
fill. The remaining failures are the MTSDF edge mask (IoU) and its edge p99;
T2-01 is implemented but **not PASS**.

## Tests and checks

- `chronon3d_c_abi_tests`: **500/500 cases, 10,972/10,972 assertions**, exit 0.
- `chronon3d_compiler_boundary_tests`: **15/15 cases, 196/196 assertions**, exit 0.
- `git diff --check`: exit 0.
- No commit, push, threshold change or corpus edit was made.

## Remaining gate

The ADR-040 exception remains because the frozen pixel corpus is still 1/8.
The three feature gates are no longer missing, but their pixel parity is not
within the frozen thresholds. Closing the exception still requires a raster
authority whose glyph edge coverage, effect composition and 3D projection match
Blend2D within the unchanged limits.
