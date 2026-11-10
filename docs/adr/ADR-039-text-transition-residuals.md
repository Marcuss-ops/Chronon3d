# ADR-039 — Text transition residuals and selector scope

| Field | Value |
|---|---|
| **Status** | Accepted — 2026-11-09 |
| **Date** | 2026-11-09 |
| **Deciders** | Chronon3D text/runtime |
| **Tags** | `text`, `transitions`, `scope`, `typewriter`, `scramble`, `line-anchor`, `gpu-fast-path` |
| **Related** | `TICKET-TEXT-CLOSURE-20261109`, `TICKET-TEXT-ANIMATOR-PARITY-20261107`, `ADR-038` |

## Context

`TICKET-TEXT-CLOSURE-20261109` left four T2 residuals open. Each one was written
as a decision point, not as an accepted defect:

- T2-01 — `text_transitions` combined with styled spans / glyph paint ranges.
- T2-02 — typewriter frame 0 and scramble outgoing width.
- T2-03 — line anchor selector scope.
- T2-04 — GPU fast path narrower than per-glyph MTSDF.

Leaving them as unspecified "open" items makes the parent ticket impossible to
close and invites accidental support claims. This ADR records the product and
architecture decision for each.

## Decision

### T2-01 — Per-keyframe spans are a non-goal

`text_transitions` + `text_spans` / `svg_glyph_paint_ranges` remains
fail-closed. The compiler keeps rejecting the combination with an explicit
error.

Rationale:

- Range validation is defined against the base authoring string. During a
  transition the visible string is generated per frame; a range that is valid
  on the base can silently select the wrong cluster or exceed the generated
  text.
- A correct implementation requires a full `TextDefinition` per keyframe,
  per-keyframe range validation, and an `AnimatedTextDocument` payload that
  carries spans and paint ranges. There is no current product use case that
  justifies that model expansion.
- Reusing the base spans would be an implicit, unvalidated contract. That is
  worse than a documented rejection.

Revisit only with a concrete editorial use case and a schema/decoder design
that validates each range against the text of its own keyframe.

### T2-02 — Typewriter frame 0 and scramble width

**Typewriter frame 0 is fixed.** A transition with zero revealed grapheme
clusters now produces an empty transition string. Both backends treat a
zero-glyph run as a successful no-op:

- software `prepare_text_run` returns `Outcome{0}` before scratch acquisition;
- native MTSDF `draw_cached_text_run_mtsdf` returns `Outcome{0}` before cache or
  atlas lookup.

The old synthetic `" "` changed the authored layout and made frame 0 depend on a
fake glyph. It is removed. `ActiveTextState` carries
`transition_text_authoritative`: Scramble/Morph/Typewriter set it, so an empty
transition string is a real target and never falls back to the full `active`
document. The rebuild path accepts the empty target and compiles a zero-glyph
layout, which is what both backends turn into their explicit no-op.

**Scramble width is covered by the existing per-frame layout + bounds union.**
`select_target_text()` returns the generated scramble string for the frame;
`apply_active_state_to_text_run_shape()` rebuilds the active layout from that
string. `compute_text_run_visual_bounds()` unions the active/transition glyphs
with the outgoing `dissolve_layout` and applies the shadow halo. Therefore the
raster bbox contains both the intermediate scrambled text and the outgoing text,
not just the first layout. The regression contract is the existing
`TextRunDriver: Scramble transition rebuilds layout with transition_text` test
plus the crossfade bounds tests. No second max-width cache is introduced: the
layout is the single width authority.

### T2-03 — Line anchor remains run-level

`LineAnchorProperty` stays run-level. Per-line/per-selector line anchoring is a
product feature, not a correctness gap: the current contract is explicit, and
the selector stack already offers line units for other properties. A per-line
evaluator would need a new line-scoped anchor model and would duplicate the
existing line map. It is not implemented in this closure.

### T2-04 — GPU fast path remains a P3 optimization

The run-level GPU fast path is intentionally narrower than the per-glyph MTSDF
route. The per-glyph route is the complete correctness fallback. There is no
evidence that widening the fast path is worth the extra case matrix; it is a
performance optimization, not a parity bug. No benchmark is claimed in this
closure.

## Consequences

### Positive

- The parent ticket can close without pretending per-keyframe spans or per-line
  anchors exist.
- The typewriter frame-0 contract is now backend-symmetric.
- Scramble width has one authority (the per-frame layout) and no second cache.

### Negative / migration cost

- Plans that combine transitions with styled spans continue to fail closed.
- Per-line line anchoring remains unavailable.
- The GPU fast path keeps its narrower case matrix.

### Neutral

- No new schema fields, cache, resolver or dependency.
- No behavior change for plans that do not use these combinations.

## Demolition Debt

None: this ADR records non-goals and one completed fix, not a compatibility
shim. If T2-01 or T2-03 is funded later, the existing fail-closed checks and
run-level contract are the removal points.

## References

- `Chronon3d/src/text/driver/text_state_sampler.cpp`
- `Chronon3d/src/text/animated_text_document.cpp`
- `Chronon3d/src/text/text_run_geometry.cpp`
- `Chronon3d/src/render_graph/nodes/text_run/text_run_execution.cpp`
- `Chronon3d/src/render_graph/nodes/text_run/text_run_draw.cpp`
- `Chronon3d/src/backends/software/processors/text_run/text_run_processor/prepare.cpp`
- `Chronon3d/tests/text/test_text_run_driver.cpp`
- `Chronon3d/tests/text/test_draw_text_run_crossfade_stroke.cpp`
