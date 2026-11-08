# ADR-037 — Pre-shaping selector unit map: selector-driven Character Offset / Character Value

| Field | Value |
|---|---|
| **Status** | Proposed (design only; no production code in this ADR) |
| **Date** | 2026-11-08 |
| **Deciders** | Chronon3D text/runtime |
| **Tags** | `text`, `pre-shaping`, `selectors`, `character-offset`, `character-value`, `bidi`, `layout-cache` |
| **Related** | Text-animator parity ticket P1-01/P1-02, `ADR-014` (text golden coverage), `ADR-019` (text coordinate model) |

## Context

`CharacterOffsetProperty` is a PreShaping transform: it changes the source
codepoints before HarfBuzz shapes them, so glyph choice, kerning, ligatures and
bidi reordering all see the transformed text. P1-02 (2026-11-08) made the
offset time-sampled and stateless against the authored source, but explicitly
left it **run-level**: every enabled `CharacterOffsetProperty` shifts the whole
run, regardless of the animator's selectors.

`CharacterValueProperty` (P1-01) has the same structural problem. It must
replace selected source codepoints before shaping, and "selected" is currently
defined only after shaping, because `GlyphSelectorUnitMap` is built from
`PlacedGlyphRun` clusters, word boundaries and visual lines.

The ticket's P1-01/P1-02 contract requires selector semantics on source
codepoints, including grapheme clusters, combining marks, excluded spaces and
bidi. This ADR defines the missing layer: a **pre-shaping selector unit map**
that maps selector units to source codepoints without introducing a second
shaping pipeline.

## Problem statement

The current pipeline is:

```
TextRunShape::pre_shaping_source        (stable authored string)
        │
        ├─ evaluate_pre_shaping_source(animators, source, time)   run-level
        │
        ▼
TextDocument → HarfBuzz → TextRunLayout → GlyphSelectorUnitMap → PostLayout
```

Selectors cannot be evaluated at the red arrow because their unit indices
(grapheme / word / line) are derived from shaped output. Evaluating selectors on
the *previous frame's* layout is not acceptable: the layout corresponds to the
already-transformed source, so weights would drift as the offset animates.

## Decision

Introduce a **canonical source map** built from the authored source and reuse
the existing selector math against it. Keep exactly one shaping pass for
rendering, plus one cacheable "canonical" shaping pass used only to establish
selector units.

### 1. Canonical layout and source map

For each `(pre_shaping_source, font, font_size, layout_spec)` tuple, build a
canonical layout once and cache it:

```text
CanonicalPreShapingMap
├── source              : std::string            // authored UTF-8
├── layout              : SharedTextRunLayout    // shaped canonical source
├── codepoints[]        : SourceCodepoint
│     ├── byte_offset
│     ├── byte_len
│     ├── grapheme_index
│     ├── word_index     (kNoWord for whitespace/newlines)
│     ├── line_index
│     └── semantic_id
├── unit_counts         : {grapheme, word, line}
└── canonical_layout_hash
```

`codepoints[]` is built by walking `layout.placed.clusters` in logical source
order and collapsing every cluster to its source byte range. Glyphs that share
a cluster (ligatures, marks) collapse into one `SourceCodepoint`; whitespace
that HarfBuzz omits is restored from the source walk, exactly as
`build_text_unit_map` already does for PostLayout selectors. The map never
splits a grapheme: a selector that selects part of a cluster selects the whole
cluster.

The canonical layout is an internal cache artifact. It is **not** the layout
used for rendering; it exists only to answer "which selector unit does this
source codepoint belong to?".

### 2. Selector resolution

Build a lightweight `PlacedGlyphRun` proxy with one entry per source codepoint
(identity glyph id, zero advance, `x = codepoint index`, `y = line index`) plus
the canonical `GlyphSelectorUnitMap` restricted to those codepoints. Pass the
proxy through the existing `compile_selectors` / `evaluate_compiled_selectors`
path. No new selector shape, order or combine implementation is introduced:
the same `Weight` math runs, so `Square`, `RampUp`, `Wiggly`, `Index`, `Random`
and the rest keep one authority.

The result is a per-animator vector:

```text
codepoint_weight[animator][codepoint] ∈ [0, 1]
```

computed once per frame per animator. The proxy is built only when at least one
PreShaping animator has selectors; animators without selectors keep the fast
run-level path.

### 3. Application semantics

`evaluate_pre_shaping_source()` becomes a fold over animators in stack order.
Each animator receives the current transformed source and the canonical map,
and applies its PreShaping properties to the selected codepoints:

- **Character Offset** — `shift = round(sampled_offset * weight)` per selected
  codepoint. A weight of 0 leaves the codepoint unchanged; partial weights round
  per codepoint, not on the run total.
- **Character Value** — replaces a selected codepoint with the authored
  replacement; the replacement must be one valid UTF-8 codepoint. Weight is
  applied as a discrete choice (weight ≥ 0.5 replaces, below does not), because
  a codepoint cannot be half-replaced.
- Whitespace/newlines are never shifted or replaced unless the selector
  explicitly includes them (`exclude_spaces = false`); their unit indices remain
  stable so later animators see the same map.
- Multiple animators compose in stack order. The map is rebuilt only if an
  animator changed the source length (Character Value), because byte offsets
  shift; Character Offset is length-preserving and reuses the map.

The authored `TextRunShape::pre_shaping_source` remains the immutable input.
The fold always starts from it, so frame N never sees frame N-1's output and
the cumulative-shift bug fixed in P1-02 cannot return.

### 4. Cache identity

Add the PreShaping transform digest to the layout cache key:

```text
digest = hash(
    authored_source,
    ordered [(property_kind, sampled_value, codepoint_weight_bits)],
    font_identity, font_size, layout_spec)
```

The canonical map is keyed by `(authored_source, font_identity, font_size,
layout_spec)` and survives across frames. The rendered layout is keyed by the
full digest. An offset that animates therefore produces a new rendered layout
per distinct sampled value (expected), while the canonical map is shaped once.

Cache eviction follows the existing `TextLayoutCache` policy; the canonical map
must not outlive the engine/font handles that own its `SharedTextRunLayout`.

### 5. Bidi, grapheme clusters and combining marks

- The canonical map is built in **logical source order**. Selector orders
  (`Forward`, `Reverse`, `FromCenter`) operate on logical codepoint order for
  PreShaping, because the transform happens before bidi reordering. This is a
  deliberate contract; visual-order selectors remain a PostLayout concern.
- Grapheme clusters are atomic. `Character` and `Grapheme` units share the
  cluster boundary; a selector never splits a base codepoint from its combining
  marks.
- RTL text: the canonical map is derived from the shaped logical clusters, so
  Arabic/Hebrew codepoints map to the same units that PostLayout selectors see.
  A dedicated golden test locks the logical-order choice.
- Ligatures: changing a selected codepoint may break the ligature in the
  rendered layout. That is the intended PreShaping behaviour; the canonical map
  still reflects the authored clusters.

### 6. Hot path and performance

- No selector work at all when the stack has no PreShaping property.
- No selector work when a PreShaping animator has no selectors (run-level fast
  path, current P1-02 behaviour).
- Selector weights are O(codepoints) per animator per frame; source
  transformation is O(codepoints). Reshaping happens only when the transformed
  source or cache digest changes.
- The canonical map is shaped once per source/font/layout tuple, not per frame.
- Memory: one canonical layout + one codepoint vector per active text run. The
  existing `TextLayoutCache` already owns layouts of this size; the new map is
  small relative to `PlacedGlyphRun`.

### 7. GPU parity

The transformed source feeds the same `TextDocument → shaping → layout` path on
CPU and GPU preparation. No shader or Vulkan record changes are required, so
CPU/GPU text parity is by construction. GPU-specific per-glyph effects remain
governed by the existing `can_lower_gpu_text_animation()` fail-closed predicate.

## Test contract

1. **Offset, selector-driven** — a 50% word selector shifts only the selected
   word's codepoints; the unselected suffix is byte-identical.
2. **Partial weight rounding** — weight 0.5 with offset 3 rounds per codepoint
   (2), not on the run total.
3. **Character Value** — selected codepoints are replaced before shaping;
   ligature formation changes accordingly.
4. **Combining marks** — selecting one codepoint of a grapheme selects the whole
   cluster; the output never contains a dangling combining mark.
5. **Bidi** — Arabic/Hebrew logical-order selection produces the expected
   shaped output; `Forward`/`Reverse` are locked to logical order.
6. **Statelessness** — evaluating frame 10 and then frame 0 from the authored
   source returns the original text (no cumulative shift).
7. **Cache** — same source + same weights hits the rendered-layout cache; a
   changed sampled offset misses and rebuilds only the rendered layout, not the
   canonical map.
8. **Parity** — CPU-resolved glyph states match the GPU preparation input for
   the same transformed source (existing text parity harness).

## Rollout

1. **Phase A (no behaviour change):** add `CanonicalPreShapingMap` + cache and
   a unit test that its codepoint/unit mapping matches `build_text_unit_map`
   for ASCII, UTF-8, combining marks and Arabic.
2. **Phase B:** selector-driven `CharacterOffsetProperty`, with the proxy
   selector path. Keep the run-level path for animators without selectors.
3. **Phase C:** `CharacterValueProperty` on the same map.
4. **Phase D:** RTL/bidi golden and cache-identity certification.
5. **Phase E:** GPU parity run on the text parity harness.

P1-02's run-level contract stays valid until Phase B lands; the ticket records
the run-level choice explicitly so it is not mistaken for selector support.

## Consequences

### Positive

- Removes the last architectural blocker for P1-01/P1-02 selector semantics
  without a second shaping or selector implementation.
- One selector math authority for PreShaping and PostLayout.
- Canonical map is cacheable, so animated offsets do not reshape twice per
  frame.
- Bidi/cluster behaviour is decided once and testable.

### Negative / migration cost

- One extra canonical shaping pass per new source/font/layout tuple, cached.
- A second layout object per active run while the map is warm.
- The proxy `PlacedGlyphRun` is a testable shim; it must stay private to the
  text runtime and never leak into the public SDK.
- Character Value can change source length, forcing a map rebuild and a
  rendered-layout miss; this is inherent to codepoint substitution.

### Neutral

- No public SDK type changes; the map lives under `src/text/`.
- No selector enum or schema changes; V3 property names are unchanged.
- No GPU shader changes.

## Alternatives considered

- **A. Evaluate selectors on the previous frame's layout.** Rejected: weights
  drift with the animated offset and can select the wrong codepoints after a
  length change.
- **B. Build units from raw Unicode segmentation without shaping.** Rejected:
  word/line boundaries for CJK/Thai and bidi runs would diverge from the
  PostLayout selector map; two authorities would drift.
- **C. Apply PreShaping properties per glyph after shaping.** Rejected
  architecturally: different codepoints change glyph choice and metrics; this is
  the bug FASE 2a fixed.
- **D. Keep run-level only and document selectors as unsupported.** This is the
  current P1-02 state; rejected for P1-01/P1-02 completion because the ticket's
  contract is selector-driven.

## References

- `Chronon3d/docs/tickets/TICKET-TEXT-ANIMATOR-PARITY-20261107.md` §P1-01/P1-02
- `Chronon3d/src/text/animation/text_pre_shaping.cpp`
- `Chronon3d/src/text/glyph_selector.cpp` (`build_text_unit_map`)
- `Chronon3d/src/text/text_run_driver.cpp` (`pre_shaping_source` lifecycle)
- `Chronon3d/include/chronon3d/text/glyph_selector.hpp`
- `Chronon3d/docs/adr/ADR-019-text-coordinate-model.md`
