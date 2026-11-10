# TICKET-TEXT-ANIMATOR-PARITY-20261107 — After Effects text-animator parity

**Status:** CLOSED — product exception (ADR-040, 2026-11-10). P0/P1 implementation remains as recorded below; the earlier close accepted incomplete CPU/GPU pixel parity and GPU fail-closed policies, and the reopened parity gates are now accepted/deferred by explicit product decision, not PASS. See [`TICKET-TEXT-CLOSURE-20261109.md`](TICKET-TEXT-CLOSURE-20261109.md), §17, and [`ADR-040`](../adr/ADR-040-text-parity-product-exception.md); P2-03 GPU shadow/stroke behavior and the T2-01 transition/span combination remain unimplemented/deferred.
**Residual closure:** caret RTL functional tests and multi-shadow dispatch are implemented. Production pixel parity remains below the frozen thresholds; ADR-040 records the product exception for this closeout. This is not a full-parity PASS.
**Owner:** Chronon3D text/runtime.
**Scope:** animated text layers, not full SVG text. SVG-specific gaps remain in
[`TICKET-SVG-TEXT-PARITY-REMAINING.md`](TICKET-SVG-TEXT-PARITY-REMAINING.md) and
[`TICKET-SVG-TEXT-ADVANCED-SCOPES.md`](TICKET-SVG-TEXT-ADVANCED-SCOPES.md).

---

## 0. How to read this ticket

This is the single master ticket for After Effects-style text animators. It has
two jobs:

1. Record **everything that already exists**, so it is not reimplemented.
2. Record **everything that is still missing**, split into P0/P1/P2, with a
   concrete step-by-step execution order.

P0 is the only scope implemented in this session. P1/P2 are deliberately
listed but not implemented here. No existing property or path was duplicated.

---

## 1. Already implemented — do not reimplement

### 1.1 Canonical property model

`Chronon3d/include/chronon3d/text/animation/text_animator_properties.hpp`
contains the single `TextAnimatorProperty` variant. Before this ticket it had
15 alternatives:

| Area | Existing properties |
|---|---|
| Transform | `position`, `scale`, `rotation`, `skew`, `anchor`, `baseline_shift` |
| Appearance | `opacity`, `blur`, `fill_color`, `stroke_color`, `stroke_width` |
| Background | `background_color`, `background_padding_em`, `background_radius_em` |
| Layout | `tracking` (cumulative PostLayout advance) |
| PreShaping | `character_offset` (animated, time-sampled source codepoint shift) |
| Material | `gel_light_direction` (run-level GEL light) |

The P0 change adds two more alternatives at the end of the variant, preserving
the indices of every existing alternative:
`WordSpacingProperty` and `LineSpacingProperty`.

### 1.2 Selector model

Existing selector support in `glyph_selector.hpp` / `glyph_selector_compile.cpp`:

- Units: `glyph`, `grapheme`, `character`, `word`, `line`.
- Shapes: `square`, `ramp_up`, `ramp_down`, `triangle`, `round`, `smooth`,
  `wiggly`, `noise`.
- Orders: `forward`, `reverse`, `from_center`, `to_center`, `random`, `index`.
- Combine modes: `replace`, `add`, `subtract`, `intersect`, `min`, `max`.
- `start` / `end` / `offset` / `amount` tracks, `exclude_spaces`,
  `end_inclusive`, `randomize_order`, seeds, `ease_low` / `ease_high`,
  `index_step`.
- Named spans via `semantic_id` and `TextSpanOverride`.
- `GlyphSelectorUnitMap` already supplies `glyph_to_word`, `glyph_to_line`,
  `first_glyph_for_word` and `first_glyph_for_line`.

### 1.3 Evaluation and composition

- `evaluate_animator_stack()` and the hot in-place
  `evaluate_animator_stack_into()` already evaluate arbitrary stacked animators
  and selectors.
- `TextPropertyBlendMode` already supports `Add`, `Replace`, `Multiply`.
- `TrackingProperty` already owns the only cross-glyph cumulative accumulator;
  the P0 implementation follows that same apply-then-carry pattern without
  duplicating the tracking path.
- `TextRunDriver` already consumes the resolved `GlyphInstanceState` vector.

### 1.4 RenderPlan / V3 serialization

- V3 `animator_property_track` already serializes 22 property names.
- `decode_text_animator()` already validates arity, bounds, uniqueness,
  finite values, linear baked keyframes, and selector/span references.
- `compile_text_animator()` already maps every V2/V3 track to the canonical
  variant.
- `AnimationPropertyKind` already owns the shared name → kind taxonomy.
- The V2 enum is intentionally frozen; all new properties are V3-only.

### 1.5 Rendering and material

- `LayerStylePlan` / `TextMaterial` already cover fill gradient, stroke,
  single/multiple CPU shadows, glow, background, GEL surface, and extruded text.
- The canonical MTSDF Vulkan route already consumes the resolved
  `GlyphInstanceState` per glyph (`make_gpu_glyph_placement()` uses
  `position`, `baseline_shift`, `scale`, `anchor`, and `layout_position`), so a
  PostLayout property that mutates `position` is Vulkan-parity by construction.
- The run-level GPU fast path is intentionally narrower: one full-run word
  selector plus position/scale/opacity/fill/tracking/blur/rotation.
- Typewriter reveal, scramble/morph helpers, text counter, and typography
  presets exist in C++. Text transitions and counters are serialized through
  V3; typography presets remain in their separate ticket.

### 1.6 Existing tickets to keep separate

| Ticket | Overlap | Rule |
|---|---|---|
| `TICKET-AFTER-EFFECTS-2_5D-PARITY.md` | 2.5D and transform parity | this ticket owns text-animator properties only |
| `TICKET-SVG-TEXT-PARITY-REMAINING.md` | SVG text gaps | no SVG work here |
| `TICKET-SVG-TEXT-ADVANCED-SCOPES.md` | advanced SVG text | no SVG work here |
| `TICKET-TEXT-QUALITY-V2.md` | shadow/stroke/MSDF quality | quality, not property coverage |
| `TICKET-MOTION-COMPOSITING-PRESETS.md` | typography presets | preset serialization remains there |

---

## 2. Gap inventory

Status values: **DONE** = implemented in this session; **OPEN** = not yet
implemented; **PARTIAL** = some code exists but a documented contract is
missing.

| ID | Gap | AE analogue | Phase | Status |
|---|---|---|---|---|
| P0-01 | Word spacing as a selector-driven, animated property | Word Spacing | PostLayout | **DONE** |
| P0-02 | Line spacing as a selector-driven, animated property | Line Spacing | PostLayout | **DONE** |
| P1-01 | Character Value / arbitrary codepoint substitution per selector | Character Value | PreShaping | **DONE (selector-scoped, ADR-037)** |
| P1-02 | Fully animated Character Offset (all keyframes, selector-driven) | Character Offset | PreShaping | **DONE (selector-scoped, ADR-037)** |
| P1-03 | Character Alignment / per-character anchor alignment | Character Alignment | PostLayout | **DONE** |
| P1-04 | Line Anchor (top/center/bottom) | Line Anchor | PostLayout | **DONE** |
| P1-05 | Anchor Point Grouping (character/word/line/all) | Anchor Point Grouping | PostLayout | **DONE** |
| P1-06 | Per-glyph shadow and per-glyph glow | Per-character shadow/glow | PostRaster/PostLayout | **DONE (software + native MTSDF)** |
| P1-07 | Per-glyph gradient fill | Gradient Fill per character | PostLayout | **DONE (software + native MTSDF)** |
| P1-08 | GPU-native per-glyph stroke/background/skew | — | PostLayout | **DONE (GPU-native)** |
| P1-09 | V3 serialization of typewriter / scramble / morph | Typewriter / Scramble | PreShaping/PostLayout | **DONE** |
| P1-10 | Text cursor / caret model | Cursor | PostLayout | **DONE (software + native shader)** |
| P2-01 | Per-glyph 3D rotation beyond the MTSDF limits | 3D rotation | PostLayout | **DONE (native exact orthographic)** |
| P2-02 | Full SVG text parity | — | — | separate ticket |
| P2-03 | Advanced text quality (canonical shadow, vector stroke, msdfgen) | — | — | separate ticket |
| P2-04 | RTL/bidi selector-order audit | — | — | **DONE** |
| P2-05 | Paragraph reflow from spacing (PreLayout evaluator) | — | PreLayout | **DONE** |

### 2.1 Reflow contract (P2-05)

`ParagraphStyle::word_spacing` is added to the advance of whitespace clusters in
`build_clusters()` before line breaking, justification and bounds. `line_spacing`
is added between composed lines in `finalize_lines()`, never after the last line.
Both fields are part of `TextLayoutCacheKey` and its V2 digest.

`update_text_run_shape_per_frame()` evaluates the `ReflowWordSpacingProperty` and
`ReflowLineSpacingProperty` alternatives before PostLayout, adds them to the
authored `reflow_*_base` values, and rebuilds the layout through the normal
`compile_text_document_layout()` path when the composed spacing changes. This is
true paragraph reflow, not a post-composition glyph offset. The legacy P0
`WordSpacingProperty`/`LineSpacingProperty` remain PostLayout selector effects for
backward compatibility.

---

## 3. P0 implementation contract

### 3.1 Word Spacing

- Type: `WordSpacingProperty` with `AnimatedValue<f32> pixels`.
- Phase: PostLayout, transform blend mode.
- Unit boundary: `GlyphSelectorUnitMap::glyph_to_word`.
- Semantics: a gap is inserted **after** each selected word. The gap is
  `pixels * weight_of_previous_word`, then carried cumulatively to every later
  glyph, including whitespace and later words.
- First word never shifts. A selected last word produces no visible shift
  because there is no following word.
- Partial weights scale the gap; animated values are sampled per frame.
- Does not change line breaking or shaping.

### 3.2 Line Spacing

- Type: `LineSpacingProperty` with `AnimatedValue<f32> pixels`.
- Phase: PostLayout, transform blend mode.
- Unit boundary: `GlyphSelectorUnitMap::glyph_to_line`.
- Semantics: a gap is inserted **after** each selected line. The gap is
  `pixels * weight_of_previous_line`, then carried cumulatively to every later
  line.
- First line never shifts. Wrapped visual lines use the same geometry-derived
  `glyph_to_line` mapping as line selectors.
- Does not change paragraph composition or layout reflow.

### 3.3 Blend behavior

`Replace` writes the cumulative offset onto `position.x` (word) or
`position.y` (line); `Add` adds it. This mirrors the existing tracking
accumulator and keeps multi-animator stacks predictable.

### 3.4 Wiring completed

- `TextAnimatorProperty` variant: two new alternatives at the end.
- `text_animator_evaluator.cpp`: per-animator cumulative accumulators.
- `text_property_applier.cpp`: explicit no-op branches with a comment pointing
  to the evaluator (the unit map is not available in the dispatcher).
- `AnimationPropertyKind`: `WordSpacing`, `LineSpacing` + name mapping.
- `compile_text_animator()`: both V3 tracks map to the new properties.
- V3 decoder: both names accepted with scalar arity and `[-1000, 1000]` bounds.
- V3 schema: enum entries and numeric bounds.
- V2 schema/decoder: unchanged and intentionally still rejects the new names.

---

## 4. Vulkan parity contract

The native MTSDF route does **not** re-evaluate text animators. It consumes the
canonical resolved `GlyphInstanceState` records. `make_gpu_glyph_placement()`
already includes `glyph.position` and `glyph.baseline_shift` in the per-glyph
translation, and `text_run_draw.cpp` already packs `state.position` into each
per-glyph dynamic record.

For paragraph spacing:

1. `ReflowWordSpacingProperty` and `ReflowLineSpacingProperty` reflow the layout
   before PostLayout and before the native route consumes it.
2. The cache key and V2 digest include `word_spacing` and `line_spacing`.
3. The native Vulkan path consumes the resulting placed glyphs; no second
   spacing implementation exists, so CPU/GPU cannot drift.
4. Per-glyph shadow, glow, gradient fill and caret are accepted by
   `can_lower_gpu_text_animation()` and lowered into the native MTSDF pass.
   Invalid parameters stay fail-closed; the synthetic Vulkan canary pins the
   native shadow, glow, gradient and caret layers.

---

## 5. Verification gates

Run with bounded concurrency to avoid OOM on the current 29 GB host:

```bash
cmake --build Chronon3d/build/chronon/linux-video-release \
  --target chronon3d_text_animator_property_tests chronon3d_c_abi_tests \
           chronon3d_render_graph_tests -j2
```

Focused checks:

1. `text_animator_property_tests` — word spacing cumulative, line spacing
   cumulative, partial selector weight, animated value.
2. `test_gpu_text_run_placement` — resolved spacing states remain lowerable to
   native MTSDF and the per-glyph placement consumes the offset.
3. `test_render_plan_decoder_features` — V3 decode of both tracks; out-of-range
   scalar rejected.
4. V2 frozen-enum test remains green.

Record the exact target names and results in the session close-out. If a target
name differs, use `cmake --build ... --target help` rather than launching a
full build.

### Session evidence (2026-11-08)

The 2026-11-07 note below about the test executables not being run is superseded.

- `cmake --build build/chronon/linux-video-release --target
  chronon3d_text_animator_property_tests -j4`: 401/401 step, exit 0, 54.16 s,
  peak RSS 1.46 GB (bounded concurrency, no OOM pressure).
- `chronon3d_text_animator_property_tests`: **39/39 cases, 178/178
  assertions**, exit 0.
- Filter `--test-case='*Spacing*'`: **3/3 cases, 11/11 assertions** (cumulative
  word spacing, selector weight + animated value, cumulative line spacing).
- The V3 decoder and V2 frozen-enum gates listed above were **not run in this
  session**; they belong to their own targets and were not rebuilt here.

### P1-02 evidence (2026-11-08)

- Build `-j14` of `chronon3d_core_tests`, `chronon3d_c_abi_tests` and
  `chronon3d_text_animator_property_tests`: **582/582 steps, exit 0**.
- `chronon3d_core_tests --test-case='*CharacterOffset*'`: **25/25 cases,
  35/35 assertions**.
- `chronon3d_c_abi_tests --test-case='*character_offset*'`: **1/1 case,
  7/7 assertions** (multi-keyframe acceptance, fractional and out-of-range
  rejection).
- `chronon3d_text_animator_property_tests`: **44/44 cases, 213/213
  assertions**.

### P1-04 evidence (2026-11-08)

- Build `-j14` of `chronon3d_core_tests`, `chronon3d_c_abi_tests` and
  `chronon3d_text_animator_property_tests`: **583/583 steps, exit 0**.
- `chronon3d_text_animator_property_tests`: **46/46 cases, 223/223
  assertions** (two new Line Anchor cases: Top/Center/Bottom shifts
  `12/-6/-24` with `ascent=12`, `total_height=36`; composition with
  `LineSpacing=8` yields y `12/20/28`, so line 0 is not shifted twice).
- `chronon3d_c_abi_tests --test-case='*line_anchor*'`: **1/1 case, 7/7
  assertions** (mode 2 accepted; 3, 1.5 and multi-keyframe rejected).
- `chronon3d_c_abi_tests`: **454/454 cases, 10070/10070 assertions**.
- `chronon3d_core_tests --test-case='*CharacterOffset*'`: **25/25 cases,
  35/35 assertions** (P1-02 regression gate).

### P1-01 / P1-03 / P1-05 evidence (2026-11-09)

- Build `chronon3d_text_animator_property_tests`: **409/409 steps**, exit 0.
- Build `chronon3d_core_tests` + `chronon3d_c_abi_tests`: **580/580 steps**,
  exit 0.
- `chronon3d_text_animator_property_tests`: **51/51 cases, 256/256
  assertions**; filter `*P1-03*,*P1-05*`: **5/5 cases, 33/33 assertions**
  (line-edge alignment with full/half weight; character, word and word+scale
  grouping pivots).
- ADR-037 selector-scoped follow-up: **49/49 cases, 76/76 assertions** in
  `test_character_offset_pre_shaping`, plus the driver integration above.
- `chronon3d_core_tests --test-case='*CharacterValue*'`: **7/7 cases, 11/11
  assertions** (replacement, whitespace preservation, UTF-8 substitution,
  invalid scalars, offset-then-value composition, no-op zero).
- `chronon3d_c_abi_tests --test-case='*character_value*,*character_alignment*,*anchor_grouping*'`:
  **3/3 cases, 17/17 assertions** (accept + integer/mode/range rejection).
- V3 extension schema parses as JSON after the three enum and `allOf`
  additions.

### ADR-037 evidence — selector-scoped Character Value / Offset (2026-11-09)

- Targeted `test_character_offset_pre_shaping` build: **49/49 cases, 76/76
  assertions**; ADR-037 filter **8/8 cases, 20/20 assertions** (word offset,
  character value, 50%-weight +3 rounding to 2, ligature, combining marks,
  50/49 threshold, statelessness, Reverse logical order on RTL).
- `test_text_run_driver` selector-scoped CharacterOffset integration: **1/1
  case, 5/5 assertions** (authored-source map, `AB CD` → `BC CD`, cached map
  source, second frame does not accumulate the transform).
- Driver cache: `TextRunShape::pre_shaping_unit_map` is keyed by the complete
  `TextLayoutCacheKey` digest (source + font + paragraph + layout options).
  When the active layout still holds the authored source the map reuses that
  layout; otherwise the driver shapes the authored source once and caches it.
- Demo 01 was re-rendered and verified: `AAAAAAA` at frame 16, `CHROAAA` at
  frame 68, `CHRONON` at frame 179.

### P2-04 evidence — RTL/bidi selector order (2026-11-09)

- `build_text_unit_map` now numbers grapheme units by sorting the unique
  cluster byte offsets ascending, instead of numbering them in glyph-array
  order. HarfBuzz emits RTL glyphs in visual order, so the old map made
  `Forward` start at the visually leading glyph rather than the logical first
  source character. Words and lines already used source byte offsets.
- New test with a visual-order RTL run (`clusters {3,2,1,0}`, source `abcd`)
  asserts `glyph_to_grapheme == {3,2,1,0}`, `grapheme_count == 4`,
  `Forward` weights the logical last character higher, and `Reverse` inverts
  that progression. This locks `Forward`/`Reverse` to logical source order;
  `FromCenter`/`ToCenter` use the same logical indices.
- `test_selector_evaluate`: **17/17 cases, 123/123 assertions**; the new RTL
  case is **1/1 case, 4/4 assertions**. ADR-037 regression rerun after the
  change: **49/49 cases, 76/76 assertions**.
- GPU parity: `text_run_packing` now packs
  `layout.units.glyph_to_grapheme[layout_index]` for Grapheme/Character
  selectors, and Vulkan unpacks that field instead of using `gg.pad0` (the
  visual glyph index). `test_gpu_text_selector_metadata`: **3/3 cases, 49/49
  assertions**; the CLI rebuilt cleanly with the graph/Vulkan changes.

### GPU-native AE parity demo (2026-11-09)

- Generator/driver: `ChrononTemplate/tools/text_animators/build_ae_parity_text_demos.py`
  (`--generate`, `--render`, `--verify`).
- Outputs: `RenderingGen/UploadDrive/ae_text_parity_20261109/`:
  `plans/*.plan.json`, `videos/*.mp4`, `stills/*.png`,
  `stills/audit/*_contact.png`, `MANIFEST.json`, `logs/*.log`.
- Three V3 plans, 1920×1080, 30 fps, 180 frames (6 s), rendered with
  `--backend vulkan --gpu-hot-path-mode require_gpu_native`, so a software
  fallback would fail the render instead of passing silently.
- Clip 1 `character_value`: seven per-glyph Character Value animators on the
  authored `CHRONON`. All glyphs start at 65 (`A`); glyphs 0..6 change to
  `C/H/R/O/N/O/N` at frames 20/34/48/62/76/90/104. Stills show `AAAAAAA` /
  `CHROAAA` / `CHRONON`, proving selector scope rather than a run-level
  substitution. Rendered on Vulkan with `require_gpu_native`, 180 frames in
  2.97 s; the log has no software fallback.
- Clip 2 `character_alignment`: three rows labelled LEFT/CENTER/RIGHT, each
  with two `X` markers; the selected marker travels to `line_left`, the line
  centre and `line_right` as `amount` ramps 0→100→0. Measured peak bounds:
  LEFT x≈600–677, CENTER marker x≈900, RIGHT x≈1242–1317.
- Clip 3 `anchor_grouping` (Word): three disjoint selector windows pulse each
  word once with scale 1→0.70→1 and rotation 0→8°→0; both word edges move
  symmetrically around the word box centre, and the rotation+anchor
  combination now renders on the native path.
- All three videos probe as 1920×1080, 180 frames, 6.0 s, ≈3–4 s render time
  each on the Vulkan device.
- Focused regressions re-run 2026-11-09 from `Chronon3d/` (relative `assets/`
  must resolve): `chronon3d_text_animator_property_tests` P1-03/P1-05 5/5
  cases, 33/33 assertions; `chronon3d_core_tests` CharacterValue 7/7 cases,
  11/11 assertions; `chronon3d_render_graph_tests` `*anchor*` 7/7 cases,
  94/94 assertions; `chronon3d_c_abi_tests` 3/3 cases, 17/17 assertions.
- Drive delivery 2026-11-09: folder `AE_Text_Parity_20261109`
  (https://drive.google.com/drive/folders/1kTGoXmL__daE0rleEPN9TkMEOvy6FmJX)
  with 26 artifacts (3 MP4s, 9 stills, 3 contact sheets, 3 plans, 3 logs,
  timing metadata, MANIFEST.json, generator) plus the upload manifest
  (https://drive.google.com/file/d/1B6n1PKkGdKMmrxdOohFBZrvZ9OnRYKJu/view).
  Re-uploaded after clip 01 switched to the seven selector-scoped animators;
  each file was content-addressed and byte-verified by `drive-upload`.

### Session evidence (2026-11-07)

- `chronon3d_text_core` built cleanly: 6/6 steps.
- Changed/affected translation units compiled cleanly: render-plan decoder,
  text-animator compiler, text-animator property tests, GPU placement tests,
  and C ABI decoder tests (5/5 object targets).
- Focused `-O0` runtime check over the canonical evaluator passed:
  word spacing `[0, 12, 24]`, line spacing `[0, 8, 16]`, and animated
  50%-weight word spacing `[_, 10, 20]`.
- Full test executables were not run: the test targets pull a pre-existing
  248–327-step dirty backlog. The background build was stopped when available
  RAM fell to ~7 GB to honor the no-OOM constraint; rerun the gate above once
  the build cache is clean.

---

## 6. P1/P2 execution order (step by step)

### P1-01 — Character Value

**Implemented (2026-11-09, selector-scoped via ADR-037).**

1. **DONE** — `CharacterValueProperty` holds `AnimatedValue<f32> codepoint`.
   `evaluate_pre_shaping_source()` samples and rounds it per frame, applies
   `CharacterOffset` first and then replaces selected source graphemes before
   shaping. ASCII whitespace (space, tab, CR, LF, FF, VT) is preserved unless
   the selector sets `exclude_spaces=false`; 0, surrogates and values above
   U+10FFFF are no-ops.
2. **DONE** — selector scope uses the ADR-037 canonical source map.  A
   selector resolves against one proxy glyph per source codepoint with the
   canonical cluster/word/line units; Character Value replaces the whole
   selected grapheme, so combining marks and ligature components are never
   left dangling.  Animators without selectors retain the run-level fast path.
3. **DONE** — V3 `character_value` accepts integer Unicode scalars in
   `[0, 1114111]`; schema, decoder, compiler and `AnimationPropertyKind` are
   wired.

Original execution steps (kept for the selector-scoped follow-up):

1. Add `CharacterValueProperty` with an `AnimatedValue` source-string or
   codepoint mapper; classify it `PreShaping`.
2. Extend `evaluate_pre_shaping_source()` to apply it per source codepoint.
3. Define selector semantics on source codepoints, including grapheme clusters,
   combining marks, and excluded spaces.
4. Add V3 property name, schema, decoder bounds, compiler mapping.
5. Add shaping tests for ligatures, bidi, and cluster boundaries.
6. Verify CPU and GPU see the same post-shaping text (parity by construction).

### P1-02 — Animated Character Offset

**Implemented (2026-11-08; selector-scoped 2026-11-09 via ADR-037).**

1. **DONE** — `CharacterOffsetProperty::offset` is an `AnimatedValue<f32>`
   sampled with `SampleTime` and rounded per frame. The V3 decoder accepts
   multiple integer keyframes in `[-10000, 10000]`; the compiler maps all of
   them through `animated_text_value()`.
2. **DONE** — the driver applies the sampled offset to
   `TextRunShape::pre_shaping_source` (the authored source) before shaping, so
   every frame starts from the original text and the shift never accumulates.
3. **DONE** — selectors scope the shift through the ADR-037 canonical source
   map.  Each animator compiles its selectors once per frame against the
   authored-source proxy, and applies `round(offset * weight)` per selected
   codepoint in stack order.  A zero weight leaves the codepoint unchanged;
   animators without selectors stay run-level.  `Reverse`/`FromCenter` order
   follows logical source order, because the transform precedes bidi reorder.
4. **DONE** — V3 bounds and per-keyframe integer validation; tests live in
   `test_character_offset_pre_shaping.cpp` and
   `test_render_plan_decoder.part03.cpp`.
5. **NOT KEPT** — a separate static-keyframe fast path was deliberately
   dropped: a single-keyframe `AnimatedValue` evaluates in O(1), and the driver
   already skips the layout rebuild when the sampled source is unchanged.

### P1-03 — Character Alignment

**Implemented (2026-11-09).**

1. **DONE** — `CharacterAlignmentMode { Left, Center, Right }` and
   `CharacterAlignmentProperty` are PostLayout and run-level.
2. **DONE** — `evaluate_animator()` derives one box per layout line from the
   authored `PlacedGlyphRun` (`min(x)`, `max(x + advance_x)`). Each selected
   glyph shifts by `target_x - glyph.x`, scaled by selector weight:
   `Left = line_left`, `Center = (line_left + line_right - advance) / 2`,
   `Right = line_right - advance`. `Replace` writes `position.x`, `Add`
   accumulates. No reshapping or reflow happens.
3. **DONE** — V3 `character_alignment` accepts exactly one integer mode
   keyframe at frame 0 (0..2); schema, decoder and compiler share the bound.

Original execution steps:

1. Define per-character alignment relative to the line box.
2. Add the property and a PostLayout applier that mutates `position.x`.
3. Preserve cumulative word/line boundaries after alignment.
4. Add V3 serialization and tests for mixed alignment within a line.

### P1-04 — Line Anchor

**Implemented (2026-11-08).**

1. **DONE** — `LineAnchorMode { Top, Center, Bottom }` is defined relative to
   the authored block extents: `Top = +ascent`, `Center = ascent -
   total_height/2`, `Bottom = ascent - total_height`, with the first-line
   baseline at glyph `y = 0`.
2. **DONE** — `LineAnchorProperty` is PostLayout and run-level: the evaluator
   computes one uniform vertical shift from `PlacedGlyphRun::ascent` and
   `total_height` and applies it to every glyph. Selectors do not scope it.
3. **DONE** — Line Spacing keeps its cumulative gap at zero on line 0, so the
   anchor shift and the spacing gaps compose without shifting line 0 twice.
4. **DONE** — V3 `line_anchor` accepts exactly one integer mode keyframe
   (0=Top, 1=Center, 2=Bottom) at frame 0; the schema, decoder and compiler
   share the same bound.

### P1-05 — Anchor Point Grouping

**Implemented (2026-11-09).**

1. **DONE** — `AnchorPointGrouping { Character, Word, Line, All }` and
   `AnchorPointGroupingProperty`.
2. **DONE** — a pre-pass in `evaluate_animator()` resolves each glyph's group
   id (glyph / `glyph_to_word` / `glyph_to_line` / run) and the group box from
   the placed run: `[min(x), max(x + advance_x)]` horizontally and
   `[min(y - ascent), max(y + descent)]` vertically. The pivot is written into
   `GlyphInstanceState::anchor` as `pivot - layout_position`, so the software
   matrix (`T(anchor) × R × S × T(-anchor)`) and the native placement rotate
   and scale around the group centre.
3. **DONE** — V3 `anchor_grouping` accepts exactly one integer mode keyframe
   at frame 0 (0..3); schema, decoder and compiler share the bound.
4. **DONE (2026-11-09)** — grouped rotation now renders on the native MTSDF
   path. `gpu_anchor_pivot_offset()` computes
   `run.s * (I - animator_scale * R * Sh) * anchor` from the forward
   `scale * rotation * shear` placement, and
   `can_lower_gpu_text_animation()` no longer rejects `rotation.z` with a
   non-zero anchor. `anchor.z` is still rejected; grouped scale alone was
   already native.

Original execution steps:

1. Add grouping enum (character/word/line/all).
2. Resolve a group pivot before applying scale/rotation.
3. Feed the pivot into `GlyphInstanceState::anchor`.
4. Verify MTSDF placement remains within its anchor/rotation limits.
5. Add serialization and tests.

### P1-06 — Per-glyph shadow and glow

**Implemented (2026-11-08, software; native MTSDF 2026-11-09).**
`GlyphShadowState` and `GlyphGlowState` are part of `GlyphInstanceState`. Ten V3
tracks (`glyph.shadow.color`, `offset`, `blur`, `opacity`; `glyph.glow.color`,
`radius`, `intensity`, plus the enable flags) lower through the compiler,
decoder and property applier. The software raster draws a blurred per-glyph
shadow surface and a per-glyph glow surface before the fill.

The native path extends `GlyphPaint` to 80 bytes and `GpuGlyphDynamic` to 96
bytes (std430, 24 words). `text_tile_raster.comp` evaluates shadow and glow
before the glyph-plane clip, so a displaced/blurred shadow can extend outside
the fill; both use the same alpha-lane SDF transfer as the fill, with authored
pixel blur/radius clamped to the baked distance range. `text_tile_bin.comp`
expands the tile envelope by the shadow offset/blur and glow radius. The
synthetic Vulkan canary pins a red shadow outside its fill plane and a blue
glow with distance falloff. `can_lower_gpu_text_animation()` accepts finite
enabled effects and rejects invalid blur/radius/offset values. Production
glyph CPU/GPU pixel diff remains part of the P2-03 quality audit.

### P1-07 — Per-glyph gradient fill

**Implemented (2026-11-08, software; native MTSDF 2026-11-09).**
`GlyphGradientState` carries start/end colors and angle, serialized as
`glyph.fill_gradient.*` V3 tracks. The software raster builds a `BLGradient`
per glyph and fills the glyph coverage with it. The native path now packs the
same start/end/angle into `GlyphPaint` and evaluates the gradient in
`text_tile_raster.comp` with the same plane-centre/half-diagonal geometry, so
`can_lower_gpu_text_animation()` accepts finite per-glyph gradients. The
synthetic Vulkan canary verifies a red→blue ramp across a flat MTSDF probe.
A production-glyph CPU/GPU pixel diff remains part of the P2-03 quality audit;
the native path no longer silently drops the gradient.

### P1-08 — GPU-native per-glyph stroke/background/skew

**Implemented and verified (2026-11-09).**

1. **DONE** — `GlyphPaint` is an 80-byte per-frame span parallel to the cached
   prepared glyph batch (it also carries shadow/glow); `GpuGlyphDynamic` is 96
   bytes and the tile-bin descriptor set has a fifth binding for it. Stroke
   still uses the existing run-level dynamic record fields, filled per glyph.
2. **DONE** — `text_tile_raster.comp` applies per-glyph background padding and
   rounded corners, and the inverse skew 2×2 matrix around the glyph layout
   origin. `text_tile_bin.comp` expands the tile envelope by the background
   padding and skew reach so contributors are not dropped at tile boundaries.
3. **DONE** — `can_lower_gpu_text_animation()` accepts finite background
   padding/radius and `|skew| < 89°`; the software fallback is no longer taken
   for these states.
4. **DONE (canary)** — The headless Vulkan canary dispatches the embedded
   `text_tile_bin` + `text_tile_raster` SPIR-V against a real device and now
   asserts: background visible outside the ink plane, rounded corner cut,
   outside-padding transparency, and both sides of a 15° skewed plane.

**Evidence (2026-11-09, `-j12`, no full build):**

- `glslangValidator -V text_tile_raster.comp`: exit 0.
- Standalone Vulkan canary: **1/1 case, 131/131 assertions**, exit 0.
- Host translation units `text_run_draw.cpp`, `gpu_text_run_animation.cpp`,
  `vulkan_backend_text_ops_private.cpp` and the focused render-graph test
  object: exit 0.
- Focused render-graph filter `*per-glyph*`: **6/6 cases, 57/57 assertions**
  before the gradient extension; the new gradient lowering case is compiled in
  the same TU.
- Production-glyph CPU/GPU pixel parity remains part of the P2-03 quality
  audit; the native canary is the functional contract proof, not a claim that
  every font/atlas edge case has been diffed.

### P1-09 — V3 typewriter/scramble/morph

**Implemented (2026-11-09).** V3 text layers accept an explicit
`text_transitions` block instead of overloading opacity tracks. Each keyframe
carries `{frame, text, transition, scramble, morph, typewriter}`; frames must be
strictly increasing in `[0, 1000000]`, text is capped at 65536 bytes, and the
array is capped at 256 entries. Supported transitions are `hold`, `cut`,
`dissolve_layouts`, `scramble`, `morph` and `typewriter`.

- Decoder and schema validate every transition and reject parameters attached
  to the wrong transition, unknown fields, invalid morph directions/easing,
  NaN/out-of-range scramble intensity and typewriter `chars_per_frame`, and
  duplicate or decreasing frames.
- `text_counter` and `text_transitions` are mutually exclusive. The decoder and
  compiler also fail closed when `text_transitions` is combined with
  `text_spans`/glyph paint ranges: those byte ranges are authored against the
  base text and cannot be applied safely to variable keyframe text.
- The compiler lowers every keyframe to an `AnimatedTextDocument`
  (`scramble_params`, `morph_params`, `typewriter_params`) and passes the last
  keyframe as the static fallback definition. Scramble and Morph preserve their
  existing C++ contracts; Typewriter reveals complete grapheme clusters, uses a
  single space at `mix=0` to keep the layout alive, and spreads the reveal over
  the gap when `chars_per_frame=0`.
- The whole block participates in the plan fingerprint; the V2 projection
  erases it. The existing `SourceTextTransition` numeric values are preserved
  and `Typewriter` is appended.
- The CLI routes plans containing `text_transitions` through the legacy
  `CompiledComposition` path (the same fallback used for particles) because the
  canonical `CompiledProgram` compiler does not lower the block yet. A failed
  legacy compile is a hard `compile_error`, never a silent static fallback.

**Evidence (Linux video release, `-j12`):**

- `chronon3d_c_abi_tests --test-case='*text_transitions*'`: **2/2 cases,
  41/41 assertions**. Covers decode/bounds/rejections and the full
  decode-plan → `compile_render_plan` → per-frame `evaluate` lowering
  (`" "` at frame 0, `"FI"` at frame 6, `"FINAL"` at frame 12). Two
  independent compiles produce the same fingerprint; changing
  `typewriter.chars_per_frame` changes it.
- `chronon3d_core_tests --test-case='AnimatedTextDocument: Typewriter*'`:
  **1/1 case, 8/8 assertions**. The broader `*typewriter*` filter is **19/19
  cases, 219/219 assertions**.
- Build: `126/126` focused test targets and `4/4` after the final guard change,
  both exit 0. `git diff --check` exit 0; the V3 schema parses as valid JSON.
- **Demo (2026-11-09):** `04_typewriter`, `05_scramble` and `06_morph` rendered
  with Vulkan `require_gpu_native`, 1920x1080, 30 fps, 180 frames. OCR reads
  `CHRONON 3D` on the final frame of all three; the typewriter bbox grows
  across frames and scramble frames contain deterministic noise. Drive bundle:
  https://drive.google.com/drive/folders/1kTGoXmL__daE0rleEPN9TkMEOvy6FmJX

No existing C++ preset API was removed or renamed.

### P1-10 — Cursor / caret

**Implemented (2026-11-08, software; native shader 2026-11-09).**
`TextCaretSpec` is part of the text-run shape and `LayerStylePlan`; V3
`style.text_caret` decodes in the layer-style path, participates in the
fingerprint, and is projected out of the V2 schema. `resolve_text_caret_rect()`
derives the insertion rectangle from the placed glyph run, so alignment and
wrapping are inherited from the same layout; blink is frame-period/duty based.
The software raster draws the caret after downsampling.

The native path extends `TextRunDynamic` to 160 bytes with the caret color,
layout rect and flags. `text_run_draw.cpp` resolves the caret once and only the
first page of a run carries it; `text_tile_raster.comp` draws it after the fill,
so no glyph can cover it. The synthetic Vulkan canary pins a green caret inside
its layout rect and background outside it. RTL pixel certification remains open.

### P2-01 — Per-glyph 3D rotation beyond MTSDF limits (2026-11-09)

**DONE (native exact orthographic).** The old MTSDF path converted per-glyph
`rotation.x/y` to `tan()` shears and rejected `|angle| >= 89`; it saturated at
the clamp and could not represent an edge-on or back-facing glyph plane.

The native path now uses the exact orthographic 2x2 projection of `Rx * Ry`
for per-glyph X/Y rotations at or beyond 89 degrees:

- `TextRunDynamic` keeps its 160-byte std430 layout: the two former caret
  padding words are now `animator_rotation_x/y`, gated by `effect_flags` bit
  11. The caret contract and all existing offsets are unchanged.
- `text_tile_raster.comp` undoes `Rz`, per-glyph skew, then the exact matrix;
  `text_tile_bin.comp` pads the tile envelope by the glyph-plane corner radius
  when the exact flag is set. `gpu_anchor_pivot_offset()` composes the same
  `Rz * M` matrix so a non-zero anchor remains fixed.
- `can_lower_gpu_text_animation()` now accepts every finite X/Y angle and keeps
  fail-closed rejection for non-finite values. Exactly 90 degrees is edge-on
  and therefore renders zero coverage; angles beyond 90 degrees are mirrored
  by the exact projection instead of being clamped.

Canonical `text_transitions` lowering is also complete. `StaticLayerProgram`
now carries the typed block, `lower_scene()` preserves it, and the shared
`build_text_transitions()` helper feeds both the RenderPlan and compiled-scene
routes. `SceneMaterializer` attaches the resulting `AnimatedTextDocument` to
the `TextRunBuilder` and uses the last keyframe as the static fallback. The CLI
no longer routes text-transition plans through `CompiledComposition`; particle
plans retain that legacy fallback.

**Evidence (Linux video release, `-j12`, 2026-11-09):**

- `chronon3d_compiler_boundary_tests`: **14/14 cases, 192/192 assertions**,
  including the new canonical transition test (program carries the block,
  materialized `TextRunShape` owns a two-keyframe `AnimatedTextDocument`).
- `chronon3d_c_abi_tests --test-case='*text_transitions*'`: **2/2 cases,
  41/41 assertions**.
- `chronon3d_render_graph_tests --test-case='GPU *text*'`: **12/12 cases,
  68/68 assertions**, including edge-on acceptance and the exact 120-degree
  pivot check (`cos(120) = -0.5`).
- `chronon3d_text_tile_raster_canary_tests`: **1/1 case, 154/154
  assertions**.
- Both modified shaders pass `glslangValidator -V`; Python shader-layout
  contract **6 passed**; `render_plan_preparation.cpp` compiles; `git diff
  --check` clean.

Production-glyph CPU/GPU pixel comparison remains part of P2-03, not this
feature closure.

### P2 — Later

1. **P2-01 — DONE:** per-glyph 3D rotation beyond MTSDF limits.
2. **P2-02/P2-03:** SVG text and text-quality tickets, not this ticket.
3. **P2-04 — DONE:** RTL/bidi selector ordering audit against the source-byte unit map. Grapheme indices are assigned in source-byte order; `Forward`/`Reverse` operate on logical order for RTL runs, and the MTSDF/Vulkan path consumes the same packed grapheme index instead of the visual glyph index. Covered by the RTL selector-order test.
4. **P2-05 — DONE:** PreLayout spacing reflow in the text-run driver, with the
   composer consuming `word_spacing` and `line_spacing` and the layout cache
   keyed by both values.

---

## 7. Residuals and non-goals

- P1-06, P1-07 and P1-10 are now native on the MTSDF path as well as software.
  Their native behavior is pinned by the synthetic Vulkan canary; a production
  glyph CPU/GPU pixel diff remains part of the P2-03 quality audit. Invalid
  effect parameters stay fail-closed.
- P2-01 beyond-MTSDF per-glyph 3D rotation is implemented with the exact
  orthographic matrix and fail-closed non-finite validation. The production
  glyph CPU/GPU pixel diff remains part of the P2-03 quality audit.
- Canonical `CompiledProgram` now lowers `text_transitions`; only particle
  plans still use the legacy `CompiledComposition` fallback.
- The legacy P0 Word/Line Spacing properties remain PostLayout visual offsets;
  only the new `Reflow*` properties perform true paragraph reflow.
- P1-04 Line Anchor is run-level and uses the authored `PlacedGlyphRun` extents
  (`ascent`, `total_height`); it is not selector-scoped. P1-01/P1-02 no longer
  have this restriction: their selectors resolve on the ADR-037 canonical map.
- P1-01 Character Value is selector-scoped; whitespace is preserved unless the
  selector explicitly includes it.  Character Value changes source length, so
  the authored map stays the stable codepoint index for later animators.
- P1-05 grouping is resolved per animator and overwrites
  `GlyphInstanceState::anchor`; an animator without grouping starts from a zero
  anchor, matching the existing reset-per-animator model.
- The V2 animator enum stays frozen.
- The run-level GPU fast path remains narrower than the per-glyph MTSDF route.
- No unrelated render-plan, SVG, or text-quality files are changed by this
  ticket.

### Completion audit (2026-11-08)

Implemented and verified in this session:

- **P1-06/P1-07:** per-glyph shadow, glow and gradient fill on the software
  raster path; V3 compiler/decoder/schema. Native MTSDF lowering was added and
  canary-verified on 2026-11-09 (see the native verification block above).
- **P1-10:** caret contract, V3 layer style, fingerprint and software raster.
  The native shader caret was added and canary-verified on 2026-11-09.
- **P2-05:** true PreLayout spacing reflow, composer integration and cache
  identity.
- **P1-02:** animated Character Offset (`AnimatedValue<f32>`, V3 multi-keyframe,
  stable pre-shaping source, stateless per-frame evaluation) verified by core,
  animator and decoder suites.
- **P1-04:** static Line Anchor (`Top`/`Center`/`Bottom`) with a run-level
  PostLayout shift from `ascent`/`total_height`, V3 single-keyframe enum and
  Line Spacing composition verified by animator and C ABI decoder suites.

Focused evidence (Linux video release, `-j12`):

- `chronon3d_text_animator_property_tests`: **44/44 cases, 213/213 assertions**.
- `chronon3d_single_line_composer_tests --test-case='*spacing*'`: **4/4 cases,
  11/11 assertions** (word spacing width, line-breaking reflow, inter-line
  spacing, cache digest).
- `chronon3d_render_graph_tests --test-case='*GPU text*'`: historical
  2026-11-08 baseline, **11/11 cases, 75/75 assertions**. The current predicate
  TU adds native shadow/glow acceptance and caret acceptance; it was compiled
  but the render-graph binary was not relinked in the 2026-11-09 focused pass.
- `chronon3d_c_abi_tests --test-case='*animator*'`: **9/9 cases, 49/49
  assertions**.
- `chronon3d_text_run_driver_tests`: **20/20 cases, 81/81 assertions**.
- Build: `498/498` steps for the broad focused targets and `408/408` steps
  for the final animator test rebuild, both exit 0.

Native text-effect verification (2026-11-09, Linux video release, `-j12`):

- `test_text_tile_raster_canary` standalone, rebuilt from the embedded
  `text_tile_bin`/`text_tile_raster` SPIR-V: **1/1 case, 154/154 assertions**,
  including background, skew, gradient, shadow, glow and caret probes.
- Both shaders compiled directly with `glslangValidator -V`: exit 0.
- Host TUs `text_run_draw.cpp`, `gpu_text_run_animation.cpp`,
  `vulkan_backend_text_ops_private.cpp`, `test_gpu_text_run_placement.cpp` and
  `test_shader_abi_consistency.cpp`: compiled, exit 0.
- Python ABI contract: **6 passed** (TextRunDynamic pinned at 160 bytes).

Still open: a production-glyph CPU/GPU pixel diff (P2-03) and RTL caret pixel
certification. Invalid effect parameters remain fail-closed. P2-04 is covered
by the logical-order selector test above; P1-09 is covered by the V3 transition
evidence in its section. Per-keyframe styled spans remain a non-goal:
`text_transitions` fails closed when combined with `text_spans` or glyph paint
ranges. The canonical `CompiledProgram` now carries and lowers
transitions; only particle plans use the legacy `CompiledComposition` fallback. Two demo-side limitations are documented rather
than hidden: Typewriter starts at frame 6 because the MTSDF route rejects a
zero-glyph space frame, and Scramble uses target-width outgoing text because
the tight surface is sized from the first layout.
