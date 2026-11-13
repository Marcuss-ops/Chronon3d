# TICKET-AE-SELECTOR-PARITY-20261112 — AE Range, Wiggly and Expression selectors

**Status:** CLOSED (2026-11-12, local) — selector-engine implementation and
local gates complete. The closed expression profile and Smoothness contract
are recorded in
[`ADR-041`](../adr/ADR-041-selector-expression-and-smoothness-contract.md)
(Proposed until CI promotion). This is **not** a new GPU pixel-parity claim;
[`ADR-040`](../adr/ADR-040-text-parity-product-exception.md) remains
authoritative for the frozen corpus.
**Owner:** Chronon3D text/runtime.
**Base revision:** `0ac4ef26a` plus this change.
**Scope:** text selector evaluation, V3 serialization, and the GPU run-level
lowering boundary. SVG text, GPU pixel parity, and Unicode Character Offset are
explicitly outside this ticket (see §6).

---

## 0. Result

The selector engine now covers the After Effects Range Selector fields that were
still missing, plus a per-glyph Expression Selector:

| Gate | State |
|---|---|
| Range Selector `Units: Percentage / Index` | **DONE** |
| Range Selector `Smoothness` | **DONE** |
| Wiggly: `Wiggles/Second`, `Correlation`, `Temporal Phase`, `Spatial Phase`, `Min/Max Amount`, `Lock Dimensions`, `Mode` | **DONE** |
| Expression Selector with per-glyph variables | **DONE** |
| Closed selector-expression profile (host APIs rejected) | **DONE** |
| Smoothness bounded/symmetric/monotone contract | **DONE** |
| Per-glyph X/Y exact 3D projection (P2-01) | **DONE — no MTSDF clamp residual** |
| V3 decode/validate/compile/schema/fingerprint parity | **DONE** |
| GPU run-level lowering for unsupported selector features | **FAIL-CLOSED** (by design) |
| Frozen GPU pixel corpus | **UNCHANGED — 1/8, ADR-040 exception** |

There is one selector evaluator. `glyph_selector_compile.cpp` owns the
semantics; the GPU per-glyph MTSDF path consumes the resolved
`GlyphInstanceState` values, and the wider GPU run-level path rejects selector
features it cannot represent. No second GPU selector implementation was added.

---

## 1. Canonical semantics

### 1.1 Range Selector Units

- `range_units` is `"percentage"` (default) or `"index"`.
- In `index` mode, `start`, `end`, and `offset` are logical unit indices for the
  selector's `unit` (`glyph`, `grapheme`, `character`, `word`, `line`).
- Indices are bound once per frame into the percentage domain:
  `percent = index / total_units * 100`.
- The window keeps the existing half-open range semantics. A window
  `start=0, end=3` selects three units, not four.
- `total_units` is the selector's compiled logical-unit count after the active
  unit mapping; spaces remain subject to `exclude_spaces`.
- Order, ease, amount, random permutation, `index_step`, and semantic spans are
  unchanged and compose with index ranges.

### 1.2 Smoothness

- `smoothness` is a percentage in `[0, 100]`; `0` preserves the historical hard
  window exactly.
- For an active window `[start, end]` of width `W`, the feather width is
  `feather = W * smoothness / 100` and the mask is
  `min(smoothstep((p - start) / feather), smoothstep((end - p) / feather))`.
- The mask is bounded, symmetric around the window centre, and monotone from
  each edge toward the centre. These invariants are pinned by property tests.
- The feather is a multiplier on the canonical shape, so existing shape,
  amount, and combine behavior remain authoritative.
- This is the repository's canonical contract; it is not a claim of byte- or
  pixel-identity with AE's proprietary falloff. Pixel parity remains under
  ADR-040.

### 1.3 Temporal Wiggly

`shape: "wiggly"` accepts:

| Field | Range / default | Semantics |
|---|---|---|
| `wiggles_per_second` | `[0, 1000]`, default `0` | temporal noise rate in seconds |
| `correlation` | `[0, 100]`, default `0` | `100` gives every unit the same sample; `0` leaves neighbours independent |
| `temporal_phase` | `[-36000, 36000]`, default `0` | phase offset in degrees |
| `spatial_phase` | `[-36000, 36000]`, default `0` | spatial phase offset in degrees |
| `min_amount` | `[0, 100]`, default `0` | lower bound of the modulated weight |
| `max_amount` | `[0, 100]`, default `100` | upper bound; must be `>= min_amount` |
| `lock_dimensions` | boolean, default `true` | use one noise channel |
| `wiggly_mode` | `"smooth" \| "corner" \| "jagged"`, default `"smooth"` | interpolation of the noise lattice |

The noise is deterministic for a given seed, position, and time. Because the
downstream weight is a scalar, `lock_dimensions=false` evaluates the three
independent channels and averages them; this documented projection is the only
scalar representation available to the canonical glyph state. Existing
`wiggly_frequency`, `wiggly_amplitude`, and `wiggly_seed` remain in effect.

### 1.4 Expression Selector

- `expression` is an optional string of 1..4096 bytes on any selector.
- The expression is evaluated once per glyph, after the range/order/ease base
  weight is known. Its result is a percentage and **replaces** that base weight;
  the final weight is clamped to `[0, 1]`.
- Available variables:

| Variable | Meaning |
|---|---|
| `textIndex` | 0-based logical unit index passed to the selector |
| `textTotal` | resolved total units for the selector |
| `selectorValue` | base weight before the expression, in percent (`0..100`) |
| `time` | sample time in seconds |
| `frame` | sample frame |
| `fps` | sample frame rate |

- The expression uses the existing `chronon3d::math::ExpressionParser` under
  `ExpressionProfile::Selector`; `sin`, `cos`, `abs`, `min`, `max`, `clamp`,
  `linear`, `ease`, `wiggle` and the other host-free built-ins are available.
  No second expression language was introduced.
- The accepted profile is closed and validated by the single
  `chronon3d::validate_selector_expression()` helper at decode and compile
  time. Only the six variables in the table plus `PI`, `E`, `true` and `false`
  are accepted. Host-dependent AE forms (`thisComp`, `thisLayer`,
  `thisProperty`, `layer()`, `loopIn`/`loopOut`, `valueAtTime`,
  `toComp`/`fromComp`, `posterizeTime`) are rejected explicitly, not evaluated
  as placeholder `0`.
- Empty, oversized, syntactically invalid, or unsupported expressions are
  rejected at decode/compile time. A runtime evaluation failure still returns
  weight `0` (fail-closed) rather than leaking a partial or uninitialized
  weight.

### 1.5 Serialization and identity

- The new fields are V3 `text_selector` properties:
  `range_units`, `smoothness`, `wiggles_per_second`, `correlation`,
  `temporal_phase`, `spatial_phase`, `min_amount`, `max_amount`,
  `lock_dimensions`, `wiggly_mode`, `expression`.
- Decoder validation is shape-gated: temporal Wiggly fields require
  `shape="wiggly"`; the legacy wiggly/noise parameter gates remain in force.
- The canonical fingerprint appends these fields only when they are non-default,
  behind a fixed flag word. Legacy plans therefore keep their existing byte
  stream and pinned fingerprint; plans using the new fields get a distinct,
  unambiguous identity.
- The pinned v37 golden was refreshed to `11656075431140586116` after proving the
  old pin was already stale at HEAD: rebuilding with HEAD's
  `fingerprint_values.cpp` (before this change) produced the same refreshed
  value. This is a stale-pin repair, not selector-schema drift.

---

## 2. GPU boundary

`is_full_run_selector()` in
`src/render_graph/nodes/text_run/gpu_text_run_animation.cpp` now rejects:

- index-unit ranges,
- non-zero smoothness,
- any expression,
- non-default temporal Wiggly parameters.

Those plans take the per-glyph MTSDF route, which consumes the same
CPU-resolved `GlyphInstanceState` vector. This keeps the evaluator canonical and
avoids a divergent GPU selector implementation. Plans that explicitly require
the rejected run-level fast path fail closed instead of silently changing
semantics.

The per-glyph route is still governed by the existing GPU pixel thresholds.
This ticket does not move the ADR-040 result.

---

## 3. Implementation map

| Area | File |
|---|---|
| Spec and enums | `include/chronon3d/text/glyph_selector_spec.hpp` |
| Canonical evaluator | `src/text/glyph_selector_compile.cpp` |
| V3 plan fields | `include/chronon3d/render_plan/render_plan_text.hpp` |
| Decoder and validation | `src/render_plan/render_plan_decoder_text.cpp` |
| Compiler mapping | `src/render_plan/render_plan_compiler_animation_text.cpp` |
| Fingerprint | `src/render_plan/fingerprint_values.cpp` |
| V3 schema | `schemas/json/chronon.render-plan.v3.extensions.schema.json` |
| Selector expression profile | `include/chronon3d/text/selector_expression.hpp`, `include/chronon3d/math/expression.hpp` |
| GPU run-level predicate | `src/render_graph/nodes/text_run/gpu_text_run_animation.cpp` |
| Selector tests | `tests/text/test_selector_evaluate.cpp` |
| Decoder/fingerprint tests | `tests/c_abi/test_render_plan_decoder_features.cpp`, `tests/c_abi/test_render_plan_fingerprint_shape.cpp` |

---

## 4. Verification

Build preset: `build/chronon/linux-video-release` (Release, ccache + mold),
`-j12`. Peak observed during the focused builds left more than 11 GiB available;
no OOM.

| Suite | Result |
|---|---|
| `chronon3d_text_selector_evaluate_tests` | **31/31 cases, 1,086/1,086 assertions** |
| `chronon3d_c_abi_tests` | **477/477 cases, 10,307/10,307 assertions** |
| `chronon3d_text_animator_property_tests` | **55/55 cases, 267/267 assertions** (earlier focused run; unchanged) |
| `chronon3d_gpu_text_selector_metadata_tests` | **3/3 cases, 49/49 assertions** (earlier focused run; unchanged) |
| `chronon3d_text_run_driver_tests` | **25/25 cases, 147/147 assertions** (earlier focused run; unchanged) |
| `chronon3d_render_graph_tests --test-case='GPU *text*'` | **12/12 cases, 68/68 assertions** (earlier focused run; unchanged) |

Additional checks:

- `python3 -m json.tool schemas/json/chronon.render-plan.v3.extensions.schema.json`
  — OK.
- `git diff --check` — OK.

**Re-run 2026-11-12 (tree corrente):**
`chronon3d_text_selector_evaluate_tests` **31/31 casi, 1086/1086 assert**;
`chronon3d_gpu_text_selector_metadata_tests` **3/3 casi, 49/49 assert**;
`chronon3d_c_abi_tests` **480/480 casi, 10.468/10.468 assert**;
`check_architecture.py` **218/218 PASS**. ADR-041 promotion and the ADR-040
pixel corpus remain external and are not claimed as PASS.

---

## 5. Demo renders

The canonical demo harness is
`ChrononTemplate/tools/text_animators/build_ae_parity_text_demos.py`. Plan
`07_selector_parity` renders three rows in one 1920x1080, 30 fps, 180-frame
clip:

- `WIGGLY` — temporal Wiggly with correlation, phase, min/max and lock.
- `EXPRESSION` — per-glyph sinusoidal expression.
- `INDEX SMOOTH` — index-unit range with 70% smoothness and an animated offset.

Rendered with the Vulkan backend and `--gpu-hot-path-mode require_gpu_native`:

```bash
chronon3d_cli validate \
  --plan RenderingGen/UploadDrive/ae_text_parity_20261109/plans/07_selector_parity.plan.json \
  --assets-root "$PWD/Chronon3d"
chronon3d_cli render \
  --plan RenderingGen/UploadDrive/ae_text_parity_20261109/plans/07_selector_parity.plan.json \
  --assets-root "$PWD/Chronon3d" \
  --backend vulkan --gpu-hot-path-mode require_gpu_native \
  --tone-map aces --exposure 1.0 \
  -o RenderingGen/UploadDrive/ae_text_parity_20261109/videos/ae_text_07_selector_parity_1920x1080_6s.mp4
```

| Gate | Result |
|---|---|
| Plan validation | exit `0` |
| Render | exit `0`; 180/180 frames accepted |
| Probe | H.264, `yuv420p`, 1920x1080, 30/1 fps, 180 frames, 6.000 s |
| File | 1,426,264 bytes |
| SHA-256 | `c244779c2abcd39a65aaaa255548b7b746c018965adf3d2ccffefb0d13b8a205` |
| Demo harness `--verify` | exit `0` |
| Timings | `render_ms=1608.87`, `wall_ms=3390.25` |

Evidence paths:

- Video: `RenderingGen/UploadDrive/ae_text_parity_20261109/videos/ae_text_07_selector_parity_1920x1080_6s.mp4`
- Timing sidecar: `.../videos/ae_text_07_selector_parity_1920x1080_6s.mp4.timing.json`
- Stills: `.../stills/ae_text_07_selector_parity_{early,mid,late}.png`
- Enlarged Expression row: `.../stills/ae_text_07_selector_parity_{early,mid,late}_expr_crop.png`

Visual probe of the stills: the Wiggly row changes glyph rotation/vertical
offset over time; the Expression row shows distinct per-glyph vertical phases
that change between early/mid/late; the Index Smooth row's amber, feathered
window travels from `IN…` toward `…SMOOTH`, demonstrating both index-unit
binding and smoothness. This is a demo of the selector inputs, not a new
pixel-parity measurement.

### 08 selector-closure demo

`08_selector_closure` is the compact demonstration of the three closed pieces:

- `SMOOTHNESS` — index-unit range, offset `0 → 10`, smoothness `85`; the amber
  feather travels across the word.
- `EXPRESSION` — `50 + 50 * sin(time * 2.4 - textIndex * 0.85)` evaluated per
  glyph, so the cyan phase changes independently of the base range.
- `3D EXACT` — selector `glyph` with exact orthographic X/Y projection, held at
  `120°` after the transition. The row is deliberately kept past the `89°`
  threshold instead of spending the readable part of the clip in the
  legibility-reduced shear branch. A normal caption reads
  `EXACT ORTHOGRAPHIC / Y = 120 DEG`.

Generated and rendered with:

```bash
python3 ChrononTemplate/tools/text_animators/build_ae_parity_text_demos.py \
  --render --only 08_selector_closure
```

| Gate | Result |
|---|---|
| Plan validation | `status: PASS`, `valid: true` |
| Render | exit `0`; 180/180 frames accepted |
| Probe | 1920x1080, 30/1 fps, 180 frames, 6.000 s |
| File | 1,751,945 bytes |
| SHA-256 | `ddff4c9802ee998d260373bd6b2d10a34183531bdf41e86c2ae35b5dcf85f77d` |
| GPU route | 900 `route=mtsdf` lines; no fallback |
| Demo harness `--verify` | exit `0` |
| Render wall | `15.3 s` |

Evidence paths:

- Video: `RenderingGen/UploadDrive/ae_text_parity_20261109/videos/08_selector_closure_1920x1080_6s.mp4`
- Timing sidecar: `.../videos/08_selector_closure_1920x1080_6s.mp4.timing.json`
- Plan: `.../plans/08_selector_closure.plan.json`
- Log: `.../logs/08_selector_closure.log`
- Stills: `.../stills/08_selector_closure_{early,mid,late}.png`
- Clip: <https://drive.google.com/file/d/1EL7Ie37H_zB4UarelnPVwRb9zu5VYTBI/view>

This demonstrates the closed contracts and the exact-projection path; it is not
a pixel-parity measurement against After Effects. ADR-040 remains the product
exception.

### Drive delivery

The original `AE_Text_Parity_20261109` folder was found in the Drive trash
(`trashed=true`), so it was not visible under the parent `Overlay Chronon`.
On 2026-11-12 the subfolder was recreated under the parent and the complete
delivery was uploaded there:

- Folder: <https://drive.google.com/drive/folders/1Q5gS7_sbLjthPu7nyIQgGG567a48dISa>
- 07 selector clip: <https://drive.google.com/file/d/1RUHGb6xOSOVNky5fm3AgGHNGorBQhxo_/view>
- 08 selector-closure clip: <https://drive.google.com/file/d/1EL7Ie37H_zB4UarelnPVwRb9zu5VYTBI/view>
- Local 07 receipt: `RenderingGen/UploadDrive/ae_text_parity_20261109/upload_recreated_20261112.json`
- Local 08 receipt: `RenderingGen/UploadDrive/ae_text_parity_20261109/upload_08_selector_closure_20261112.json`

The recreated folder now contains 24 artifacts and eight demo videos. The
original 16 artifacts comprise all seven 1920x1080, 30 fps, 180-frame,
6-second demo videos (01-06 plus the 07 selector-parity clip), the 07 timing
sidecar, three stills, three enlarged Expression crops, the 07 V3 plan and the
07 render log.
The 08 bundle adds the selector-closure video, its timing sidecar, the V3 plan,
the render log, three stills and `MANIFEST_08_selector_closure.json`.

A live Drive listing after the 08 upload reports `folder_count=24` and
`video_count=8`. All eight uploaded 08 files were downloaded again and their
SHA-256 values matched the local files, including video SHA-256
`ddff4c9802ee998d260373bd6b2d10a34183531bdf41e86c2ae35b5dcf85f77d`.
The previous `upload_videos_20261112.json` receipt refers to the old,
now-trashed folder and is retained only as history.

---

## 6. Closed contracts and residuals

### 6.1 Closed on 2026-11-12

1. **Selector expression profile.** The ambiguous "parser subset" is now a
   closed, validated profile. `ExpressionProfile::Selector` accepts only
   `textIndex`, `textTotal`, `selectorValue`, `time`, `frame`, `fps` and the
   host-free built-ins; host objects and keyframe helpers are rejected at
   decode and compile. This is an explicit boundary, not silent drift. Full AE
   host runtime is out of selector scope.
2. **Smoothness contract.** The smoothstep edge mask is pinned by bounded,
   symmetric and monotone property tests. The repository claims the internal
   contract, not byte/pixel identity with AE's proprietary falloff; pixel
   parity remains ADR-040.
3. **Per-glyph X/Y 3D projection.** P2-01's exact orthographic path is
   authoritative at and beyond the 89-degree threshold; 90-degree edge-on and
   120-degree pivot cases are covered. The stale "MTSDF limits" residual is
   removed. Only the ADR-040 production pixel diff remains.

### 6.2 Remaining residuals — not claimed as PASS

1. **GPU pixel corpus.** ADR-040 remains in force: 1/8 frozen cases pass. GPU
   canonical shadow, GPU `VectorStroke`, and T2-01 are unchanged and still fail
   closed.
2. **Unicode Character Offset.** `apply_character_offset_to_source()` shifts
   ASCII letters only; non-Latin UTF-8 passes through. General Unicode
   manipulation needs Unicode tables or an approved dependency/ADR and is not
   part of the selector evaluator.
3. **Production glyph CPU/GPU pixel diff.** This remains under ADR-040 and is
   not specific to the selector evaluator.

---

## 7. Follow-ups

- Promote ADR-041 to `Accepted` after CI covers the selector-expression and
  Smoothness tests on the published SHA. The implementation itself is complete.
- Add GPU run-level support only by sharing the canonical evaluator; do not
  duplicate selector math in a shader.
- If Unicode Character Offset is required, add a table-backed implementation
  under an explicit ADR and keep the ASCII path byte-identical.
- Pixel-corpus exit conditions remain exactly those in ADR-040.

---

## References

- [`TICKET-TEXT-ANIMATOR-PARITY-20261107.md`](TICKET-TEXT-ANIMATOR-PARITY-20261107.md)
- [`ADR-040`](../adr/ADR-040-text-parity-product-exception.md)
- [`ADR-041`](../adr/ADR-041-selector-expression-and-smoothness-contract.md)
- `docs/evidence/text-parity-20261110-product-exception/REPORT.md`
