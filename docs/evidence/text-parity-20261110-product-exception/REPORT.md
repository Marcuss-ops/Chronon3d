# Text AE parity — product-exception closeout (2026-11-10)

**Status:** CLOSED BY PRODUCT EXCEPTION — this is **not** a parity PASS.

- **Decision:** [ADR-040](../../adr/ADR-040-text-parity-product-exception.md)
- **Ticket:** [TICKET-TEXT-CLOSURE-20261109](../../tickets/TICKET-TEXT-CLOSURE-20261109.md)
- **Build:** `cmake --build build/chronon/linux-video-release --target chronon3d_cli -j12`
  — exit 0
- **CLI SHA-256:** `ba6e14ad788fcab97000affb177f0f7bbb367595b3ffe52dd579c6990a1da75e`
- **Corpus:** `tests/text/fixtures/pixel_parity/manifest.json`
- **Verifier:** `tools/verify_text_cpu_gpu_parity.py --repeat`
- **Raw report:** [`report.json`](report.json) (`"pass": false`)

## Result

`1/8` cases pass. The metrics below are the verifier's worst frame per case;
the full per-frame data is in `report.json`.

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

The frozen thresholds are unchanged: alpha IoU ≥ 0.995, coverage 0.99–1.01,
RGB MAE ≤ 2/255, RGB p99 ≤ 8/255, SSIM luma ≥ 0.98, PSNR luma ≥ 35 dB, bbox
slack 1 px and flat max 2/255. `multipage` is the only PASS.

## Rejected 2x2 probe

A second 2x2 coverage probe on this baseline was measured and rejected:

- `latin_basic`: IoU `0.985900 → 0.962797`, coverage `0.998673 → 1.038640`.
- `transitions`: IoU `0.983597 → 0.956730`, coverage `1.003218 → 1.045227`.

The probe widened the mask support and worsened the gate. The shader was
reverted and the CLI was rebuilt; the SHA above is the baseline binary, not the
probe. No rendering-code change from this closeout is retained.

## Accepted scope

ADR-040 accepts this residual as a product exception. It does **not** authorize:

- a threshold or metric change;
- a claim that the seven failing cases pass;
- silent fallback for GPU canonical shadow or GPU `VectorStroke`;
- support for `text_transitions` combined with spans/paint ranges;
- closure of SVG text, GEL surface or DirectYUV/NV12 scopes.

The exit conditions are in ADR-040: GPU canonical union-mask shadow, GPU
vector stroke, and a typed transition/span path, followed by a full frozen
corpus rerun on the final SHA.
