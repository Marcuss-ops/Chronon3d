# TICKET-TEXT-ANIMATOR-PARITY-20261107 — After Effects text-animator parity

**Status:** OPEN — P0 `Word Spacing` / `Line Spacing`, P1-02 animated `Character Offset`, P1-06/P1-07 software effects, P1-10 caret and P2-05 reflow implemented and verified (2026-11-08); P1/P2 backlog open.
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
  presets exist in C++ but are not all serialized through V3.

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
| P1-01 | Character Value / arbitrary codepoint substitution per selector | Character Value | PreShaping | OPEN |
| P1-02 | Fully animated Character Offset (all keyframes, selector-driven) | Character Offset | PreShaping | **DONE (run-level)** |
| P1-03 | Character Alignment / per-character anchor alignment | Character Alignment | PostLayout | OPEN |
| P1-04 | Line Anchor (top/center/bottom) | Line Anchor | PostLayout | OPEN |
| P1-05 | Anchor Point Grouping (character/word/line/all) | Anchor Point Grouping | PostLayout | OPEN |
| P1-06 | Per-glyph shadow and per-glyph glow | Per-character shadow/glow | PostRaster/PostLayout | **DONE (software)** |
| P1-07 | Per-glyph gradient fill | Gradient Fill per character | PostLayout | **DONE (software)** |
| P1-08 | GPU-native per-glyph stroke/background/skew | — | PostLayout | PARTIAL |
| P1-09 | V3 serialization of typewriter / scramble / morph | Typewriter / Scramble | PreShaping/PostLayout | PARTIAL |
| P1-10 | Text cursor / caret model | Cursor | PostLayout | **DONE (software)** |
| P2-01 | Per-glyph 3D rotation beyond the MTSDF limits | 3D rotation | PostLayout | OPEN |
| P2-02 | Full SVG text parity | — | — | separate ticket |
| P2-03 | Advanced text quality (canonical shadow, vector stroke, msdfgen) | — | — | separate ticket |
| P2-04 | RTL/bidi selector-order audit | — | — | OPEN |
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
4. Per-glyph shadow, glow, gradient fill and caret are rejected by
   `can_lower_gpu_text_animation()` and take the software path until the native
   MTSDF pass has pixel parity. They are never silently dropped.

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

1. Add `CharacterValueProperty` with an `AnimatedValue` source-string or
   codepoint mapper; classify it `PreShaping`.
2. Extend `evaluate_pre_shaping_source()` to apply it per source codepoint.
3. Define selector semantics on source codepoints, including grapheme clusters,
   combining marks, and excluded spaces.
4. Add V3 property name, schema, decoder bounds, compiler mapping.
5. Add shaping tests for ligatures, bidi, and cluster boundaries.
6. Verify CPU and GPU see the same post-shaping text (parity by construction).### P1-02 — Animated Character Offset

**Implemented (2026-11-08).**

1. **DONE** — `CharacterOffsetProperty::offset` is an `AnimatedValue<f32>`
   sampled with `SampleTime` and rounded per frame. The V3 decoder accepts
   multiple integer keyframes in `[-10000, 10000]`; the compiler maps all of
   them through `animated_text_value()`.
2. **DONE** — the driver applies the sampled offset to
   `TextRunShape::pre_shaping_source` (the authored source) before shaping, so
   every frame starts from the original text and the shift never accumulates.
3. **DECIDED** — selectors do not scope PreShaping offsets. The codepoint shift
   is a run-level transform applied before shaping, when selector glyph maps do
   not exist yet. This is the documented contract, not an accidental omission.
4. **DONE** — V3 bounds and per-keyframe integer validation; tests live in
   `test_character_offset_pre_shaping.cpp` and
   `test_render_plan_decoder.part03.cpp`.
5. **NOT KEPT** — a separate static-keyframe fast path was deliberately
   dropped: a single-keyframe `AnimatedValue` evaluates in O(1), and the driver
   already skips the layout rebuild when the sampled source is unchanged.

### P1-03 — Character Alignment

1. Define per-character alignment relative to the line box.
2. Add the property and a PostLayout applier that mutates `position.x`.
3. Preserve cumulative word/line boundaries after alignment.
4. Add V3 serialization and tests for mixed alignment within a line.

### P1-04 — Line Anchor

1. Define top/center/bottom anchors relative to the first line baseline.
2. Add a PostLayout property that shifts all glyphs by the anchor offset.
3. Compose it with Line Spacing without double-applying line 0.
4. Add V3 serialization and tests.

### P1-05 — Anchor Point Grouping

1. Add grouping enum (character/word/line/all).
2. Resolve a group pivot before applying scale/rotation.
3. Feed the pivot into `GlyphInstanceState::anchor`.
4. Verify MTSDF placement remains within its anchor/rotation limits.
5. Add serialization and tests.

### P1-06 — Per-glyph shadow and glow

**Implemented (2026-11-08, software).** `GlyphShadowState` and `GlyphGlowState`
are part of `GlyphInstanceState`. Ten V3 tracks (`glyph.shadow.color`,
`offset`, `blur`, `opacity`; `glyph.glow.color`, `radius`, `intensity`, plus the
enable flags) lower through the compiler, decoder and property applier. The
software raster draws a blurred per-glyph shadow surface and a per-glyph glow
surface before the fill. `can_lower_gpu_text_animation()` rejects any enabled
per-glyph shadow/glow, so strict-native mode falls back instead of dropping the
effect. Pixel parity on the native MTSDF shader remains the next step.

### P1-07 — Per-glyph gradient fill

**Implemented (2026-11-08, software).** `GlyphGradientState` carries start/end
colors and angle, serialized as `glyph.fill_gradient.*` V3 tracks. The software
raster builds a `BLGradient` per glyph and fills the glyph coverage with it.
Native MTSDF gradient sampling is not implemented; `can_lower_gpu_text_animation()`
rejects enabled per-glyph gradients and the renderer stays on software. CPU/GPU
pixel comparison is therefore an explicit open GPU task, not a silent fallback.

### P1-08 — GPU-native per-glyph stroke/background/skew

1. Extend `can_lower_gpu_text_animation()` and the MTSDF dynamic record.
2. Implement per-glyph stroke/background/skew in the shader.
3. Remove the software fallback only after pixel parity is proven.
4. Keep strict GPU-native fail-closed until then.

### P1-09 — V3 typewriter/scramble/morph

1. Choose an explicit V3 block rather than overloading opacity tracks.
2. Add decoder + schema + compiler lowering.
3. Preserve the existing C++ preset API.
4. Add round-trip and deterministic-render tests.

### P1-10 — Cursor / caret

**Implemented (2026-11-08, software).** `TextCaretSpec` is part of the text-run
shape and `LayerStylePlan`; V3 `style.text_caret` decodes in the layer-style
path, participates in the fingerprint, and is projected out of the V2 schema.
`resolve_text_caret_rect()` derives the insertion rectangle from the placed glyph
run, so alignment and wrapping are inherited from the same layout; blink is
frame-period/duty based. The software raster draws the caret after downsampling.
The native GPU pass does not draw it yet, and `can_lower_gpu_text_animation()`
rejects an enabled caret. RTL pixel certification remains open.

### P2 — Later

1. **P2-01:** per-glyph 3D rotation beyond MTSDF limits.
2. **P2-02/P2-03:** SVG text and text-quality tickets, not this ticket.
3. **P2-04:** RTL/bidi selector ordering audit against the source-byte unit map.
4. **P2-05 — DONE:** PreLayout spacing reflow in the text-run driver, with the
   composer consuming `word_spacing` and `line_spacing` and the layout cache
   keyed by both values.

---

## 7. Residuals and non-goals

- P1-06, P1-07 and P1-10 are complete on the software raster path only; the
  native Vulkan/MTSDF pass explicitly rejects them and stays fail-closed.
- The legacy P0 Word/Line Spacing properties remain PostLayout visual offsets;
  only the new `Reflow*` properties perform true paragraph reflow.
- P1-02 offsets are run-level: selectors are resolved after shaping, so a
  selector-scoped Character Offset would need a pre-shaping unit map and is not
  part of this contract.
- The V2 animator enum stays frozen.
- The run-level GPU fast path remains narrower than the per-glyph MTSDF route.
- No unrelated render-plan, SVG, or text-quality files are changed by this
  ticket.

### Completion audit (2026-11-08)

Implemented and verified in this session:

- **P1-06/P1-07:** per-glyph shadow, glow and gradient fill on the software
  raster path; V3 compiler/decoder/schema and GPU fail-closed predicate.
- **P1-10:** caret contract, V3 layer style, fingerprint, software raster and
  GPU fail-closed predicate.
- **P2-05:** true PreLayout spacing reflow, composer integration and cache
  identity.
- **P1-02:** animated Character Offset (`AnimatedValue<f32>`, V3 multi-keyframe,
  stable pre-shaping source, stateless per-frame evaluation) verified by core,
  animator and decoder suites.

Focused evidence (Linux video release, `-j12`):

- `chronon3d_text_animator_property_tests`: **44/44 cases, 213/213 assertions**.
- `chronon3d_single_line_composer_tests --test-case='*spacing*'`: **4/4 cases,
  11/11 assertions** (word spacing width, line-breaking reflow, inter-line
  spacing, cache digest).
- `chronon3d_render_graph_tests --test-case='*GPU text*'`: **11/11 cases,
  75/75 assertions**, including the new caret-rejection case.
- `chronon3d_c_abi_tests --test-case='*animator*'`: **9/9 cases, 49/49
  assertions**.
- `chronon3d_text_run_driver_tests`: **20/20 cases, 81/81 assertions**.
- Build: `498/498` steps for the broad focused targets and `408/408` steps
  for the final animator test rebuild, both exit 0.

Still open: native MTSDF pixel parity for per-glyph shadow/glow/gradient and a
native caret quad; RTL caret pixel certification. These are fail-closed, not
silently degraded. P1-01/P1-03/P1-04/P1-05/P1-08/P1-09 and P2-01/P2-04 remain
outside this completion.
