# Text CPU/GPU pixel parity — 2026-11-09

**Status:** FAIL — parity incomplete. The harness, frozen corpus, native-route
enforcement and repeat check are complete; the strict T0-01 thresholds are not
met by the canonical MTSDF rasterizer. The previously accepted antialias
exception is no longer an exit condition under the reopened full-parity scope.

**Ticket:** `docs/tickets/TICKET-TEXT-CLOSURE-20261109.md` (T0-01/T0-02/T0-03).

## Environment and reproduction

- Host: Linux, local Vulkan 1.3.204 instance (llvmpipe device; full
  `vulkaninfo --summary` is stored in `report.json.vulkan_identity`).
- CLI: `build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli`
- CLI SHA-256: `66b3f8470ec3088ebe3633b1934da6d81c287d5c45eb2335013197a3d4a96938`
- Raw sink: RGBA8 premultiplied, frames concatenated; `frame_bytes = width*height*4`.
- Both backends use `--profile production`; Vulkan uses
  `--gpu-hot-path-mode require_gpu_native`.
- The raw sink writes every plan frame; the manifest intentionally samples
  frames instead of using `--frames`, which does not shorten the render.

```bash
cd Chronon3d
python3 tools/verify_text_cpu_gpu_parity.py \
  --corpus tests/text/fixtures/pixel_parity/manifest.json \
  --out docs/evidence/text-parity-20261109 \
  --require-same-sha --repeat
```

The manifest freezes each plan SHA-256; `--repeat` requires byte-identical
software output across two runs. The harness rejects a Vulkan log that lacks
`route=mtsdf` or contains `native surface path fell back` /
`UnsupportedCapability`. The benign `font-fallback` warning is not a render
fallback and is not treated as one.

## Corpus result

All 8 cases rendered on both backends, repeat-identical on software, with
`route=mtsdf` on every sampled Vulkan frame. No render or route failure
remains in the corpus.

| Case | alpha IoU | coverage | RGB MAE | RGB p99 | SSIM | PSNR dB | bbox Δ max | Result |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| `latin_basic` | 0.95277 | 0.99479 | 0.02029 | 0.51373 | 0.98267 | 21.08 | 1 px | FAIL: alpha, MAE, p99, PSNR |
| `exact_3d` | 0.96649 | 0.96807 | 0.00191 | 0.03922 | 0.99969 | 43.97 | 1 px | FAIL: alpha, coverage, p99 |
| `effects` | 0.90384 | 0.90384 | 0.01716 | 0.16078 | 0.99581 | 29.08 | 4 px | FAIL: alpha, coverage, bbox, MAE, p99, flat |
| `multi_shadow` | 0.93106 | 0.93278 | 0.08742 | 0.43529 | 0.97365 | 20.90 | 3 px | FAIL: alpha, coverage, bbox, MAE, p99, SSIM, PSNR |
| `rtl_caret` | 0.91177 | 0.96960 | 0.00632 | 0.20392 | 0.97029 | 24.34 | 1 px | FAIL: alpha, coverage, p99, SSIM, PSNR |
| `transitions` | 0.98024 | 0.99530 | 0.00704 | 0.20000 | 0.99719 | 29.04 | 1 px | FAIL: alpha, p99, PSNR |
| `fractional` | 0.98464 | 0.98751 | 0.00102 | 0.02745 | 0.99981 | 42.64 | 2 px | FAIL: alpha, coverage, bbox |
| `multipage` | 0.98035 | 0.99004 | 0.00567 | 0.25098 | 0.99382 | 25.95 | 1 px | FAIL: alpha, p99, PSNR |

Thresholds are unchanged from the ticket: alpha IoU ≥ 0.995, coverage
0.99–1.01, RGB MAE ≤ 2/255, RGB p99 ≤ 8/255, SSIM ≥ 0.98, PSNR ≥ 35 dB,
bbox ±1 px, flat background ≤ 2/255 outside the 2 px edge band.

Notable passes: `exact_3d` and `fractional` reach SSIM 0.9997/0.9998 and
PSNR 43.97/42.64 dB; their remaining red is the binary-alpha mask and a
coverage shortfall. `effects` has one flat-background hit (0.023529), caused by
the shadow/glow extent; `multi_shadow` and `rtl_caret` remain below SSIM 0.98.

## Findings and fixes landed

### 1. MTSDF plane origin was quantized to em pixels

`MtsdfGlyphBitmap` stored `floor(origin_x/y)`, and `GpuGlyphMetrics` carried
integers into `GlyphLocation::plane_left/top`. At 96 px the canonical 64-em
atlas is scaled 1.5×, so the GPU text was displaced by about 2 px horizontally
and 1 px vertically.

Fix: store the fractional origin in `MtsdfGlyphBitmap`, carry it through
`GpuGlyphMetrics`, and use it for `plane_left/top/right/bottom`.

Measured effect on `latin_basic`:

| Metric | Before | After |
|---|---:|---:|
| Best alignment shift | (dx=2, dy=1) | (0, 0) |
| alpha IoU | 0.8137 | 0.9528 |
| bbox side difference | 2 px | 1 px |

Files: `include/chronon3d/text/mtsdf_glyph.hpp`,
`include/chronon3d/runtime/gpu_glyph_atlas.hpp`,
`src/backends/text/mtsdf_generator.cpp`,
`src/runtime/gpu_glyph_atlas_core_detail.hpp`,
`tests/text/test_mtsdf_generator.cpp`.

### 2. T0-03 — ordered multi-shadow is now native on Vulkan

`prepare_mtsdf_text_run` still rejected `shadows.size() > 1`, so the new
shadow-only passes could never run. The stale rejection was removed.
`text_run_draw.cpp` now emits one shadow-only pass per authored shadow in
order, then one fill pass, with a declared limit of 4 and fail-closed above it.
The shader early-out for `kTextRunEffectShadowOnly` prevents shadow passes from
drawing fill/caret.

Evidence: `multi_shadow` renders 31/31 frames with `route=mtsdf`; the red and
blue layers are both visible, in authored order, with the white fill on top.
The remaining 3 px bbox / coverage difference is the software box-blur vs the
MTSDF shader blur, tracked by T1-01; it is not a missing shadow layer.

Files: `src/render_graph/nodes/text_run/text_run_packing.cpp`,
`src/render_graph/nodes/text_run/text_run_draw.cpp`,
`src/backends/vulkan/shaders/text_tile_raster.comp`,
`include/chronon3d/runtime/render_surface.hpp`.

### 3. T0-02 — RTL caret uses logical cluster edges

`resolve_text_caret_rect()` now walks `placed.clusters` in logical order, uses
the initial/final edge for LTR/RTL, snaps inside ligatures/combining clusters,
and normalizes `caret.byte_index` to a UTF-8 boundary. `ShapedFontSpan::direction`
is populated from `ResolvedTextRun::direction`; direction is not inferred from
visual order.

Evidence: the targeted caret group passes 5/5 cases / 18 assertions, and the
full animator suite now passes 55/55 cases / 267 assertions. The rendered RTL
frame shows the Arabic and Hebrew carets at the logical right edge and the
mixed `Hello مرحبا World` caret at the run boundary. Software/Vulkan bbox is
within 1 px; the alpha IoU shortfall is glyph-edge AA, not caret placement.

Files: `include/chronon3d/text/text_run_shape.hpp`,
`include/chronon3d/text/text_run_layout.hpp`,
`src/text/compiler/text_font_span_builder.cpp`,
`tests/text/text_animator_property_tests.cpp`.

### 4. 3D fixtures require a camera

An `enable_3d` text layer without a camera projected its tight surface at the
canvas origin (both backends), which looked like a renderer clipping bug. The
corpus now uses the repo's standard perspective camera
(`position [0,0,-1000]`, `fov_deg 20.4074`, `zoom 1`) and tight text surfaces.
`exact_3d` and `fractional` then moved from clipped origin fragments to
SSIM/PSNR passes. This is an authoring rule for 3D text fixtures.

## T0-01 residual (not accepted as parity)

**Owner:** Chronon3D text/runtime (MTSDF edge coverage and canonical shadow).

The remaining red is the binary `alpha > 0` mask at glyph edges: Blend2D's
analytic coverage and the canonical 64-em MTSDF bilinear reconstruction do not
produce identical sub-pixel fringes. Even the best cases reach alpha IoU
0.98464 (fractional), while SSIM is 0.99981 and PSNR 42.64 dB. Raising
`alpha_iou` to 0.995 by dilating, blurring or thresholding the mask would hide
the difference rather than fix it, so the threshold remains unchanged.

Required work before T0-01 can be marked PASS:

1. Correct glyph edge coverage and rerun this exact corpus without changing its
   thresholds. A one-frame 256-em experiment failed to improve `latin_basic`;
   see [`profile-256-single-frame.md`](../text-parity-20261109-hq/profile-256-single-frame.md).
2. T1-01: implement canonical union-mask shadow blur on GPU; this is a major
   gap in `effects` and `multi_shadow`.
3. Complete the reopened pixel gates for RTL caret, GPU VectorStroke, and
   transition+span combinations.

## Build and tests

```text
cmake --build build/chronon/linux-video-release \
  --target chronon3d_cli chronon3d_mtsdf_generator_tests \
           chronon3d_mtsdf_font_identity_tests \
           chronon3d_text_animator_property_tests \
           chronon3d_text_tile_raster_canary_tests \
           chronon3d_render_graph_tests -j12
BUILD_EXIT=0
```

| Binary | Result |
|---|---|
| `chronon3d_mtsdf_generator_tests` | 7/7 cases, 178/178 assertions |
| `chronon3d_mtsdf_font_identity_tests` | 5/5 cases, 36/36 assertions |
| `chronon3d_text_animator_property_tests` | 55/55 cases, 267/267 assertions |
| `chronon3d_text_tile_raster_canary_tests` | 1/1 case, 154/154 assertions |
| `chronon3d_render_graph_tests --test-case='GPU *text*'` | 12/12 cases, 68/68 assertions |

## Artifacts

- `report.json` — per-frame metrics, thresholds, CLI SHA, plan SHAs, Vulkan
  identity, repeat result.
- `<case>/software.raw`, `<case>/vulkan.raw`, `*.timing.json` — raw evidence.
- `<case>/software.log`, `<case>/vulkan.log` — route and render logs.
- `<case>/diff_*.png` — 8× amplified diff for failing sampled frames.
- `tests/text/fixtures/pixel_parity/manifest.json` — frozen case/frame/ROI/SHA
  manifest.

No commit was made in this session.
