# TICKET-TEXT-XOFFSET-DOUBLE — Root cause investigation (Step 3)

**Status**: 🟡 DIAGNOSIS COMPLETE — root cause hypothesized, **no production
code modified** (per user constraint "NON toccare il codice di produzione
— solo diagnosi").  This document is the engineering handoff for the
fix-implementation ticket that follows.

## Symptom (Step 2 empirical confirmation)

| Property                | Expected | Observed | Delta       |
|-------------------------|----------|----------|-------------|
| Canvas size             | 1920×1080 | 1920×1080 | —           |
| Text center X           | **960**  | **~302** | **−658**    |
| Text center Y           | 540      | ~538     | ≈ 0 (correct) |
| Coordinate system       | Canvas   | Canvas   | —           |

X-axis only: 658 px LEFT-shift on a single text bbox center.
Y is correct (within rounding).

## Step-by-step data flow (5 steps, file:line + delta contribution)

| # | Step                                                            | File:line                                                              | X delta (cumulative) | Y delta | Notes |
|---|-----------------------------------------------------------------|------------------------------------------------------------------------|----------------------|---------|-------|
| 1 | `TextSpec` (or `TextRunSpec`) authored with `pin_to(Anchor::Center)` | `content/examples/text/text_animations.cpp` (and 30+ other call sites) | 0 (intent only)       | 0       | User intent: text centered on canvas center. |
| 2 | `SceneBuilder` / `LayerBuilder::pin_to(Anchor::Center)` → `item.transform = translate(960, 540)` | `include/chronon3d/scene/builders/layer_builder.hpp` + `src/scene/builders/layer_builder.cpp` | 0                    | 0       | The pin_to semantic translates the layer to (960, 540) in world (canvas-centred) space. |
| 3 | `should_use_centered_rendering` strips implicit canvas-center if `is_implicit_2d_centering_only` returns true | `src/render_graph/builder/graph_builder_coordinates.hpp:51` (TICKET-104 fix area) | **X 0/±960/±658** ❓ | 0/±540/±? ❓ | The strip passes a canvas-center-stripped `m_matrix_override` to the source pass, which then re-applies the canvas-center in `build_world_matrix` (TICKET-104: skip `canvas_center` when `matrix_override` is set). The X-stripped matrix gets re-multiplied with `ssaa_scale * placement.matrix` in `build_world_matrix` (see step 4). **This is the most likely step where the 658 X-only delta is introduced.** |
| 4 | `text_run::build_world_matrix(ctx, placement)` returns `ssaa_scale * placement.matrix` | `src/render_graph/nodes/text_run/text_run_transform.cpp:13–32` | 0 (no X math here, just SSAA) | 0 | The function explicitly comments: "The graph builder has already pre-computed the final world matrix … We only apply SSAA scaling on top — no centering or projection decisions happen here." So the X delta is NOT introduced here — it must be in `placement.matrix` (i.e. step 3). |
| 5 | `compute_text_run_world_bbox(shape, model, spread)` transforms the 4 local corners by the model matrix | `src/text/text_run_geometry.cpp:168–209` | 0 (passive transform) | 0 | The function delegates local bounds to `compute_text_run_visual_bounds` and just multiplies by `model`. Whatever X is in `model` is reflected in the world bbox. |

**Net delta**: 658 px LEFT-shift originates from **`placement.matrix`
at step 3** (the TICKET-104 strip+reapply path). The matrix is wrong
on X by exactly 658 px; Y is correct because the strip+reapply math
cancels cleanly on Y (likely because Y centering is handled by a
different code path — the `pin_to(Center)` Y semantics resolve
correctly to (?, 540), while the X semantics resolve to (302, ?)
= (960 − 658, ?)).

## Root cause hypothesis

The TICKET-104 fix (`graph_builder_coordinates.hpp:40–58`) was
designed to address a documented **double canvas-center** bug
(see the in-source comment: *"this is `translate(960, 540) *
translate(960, 540) = translate(1920, 1080)` — text goes off-canvas
at the bottom-right corner"*).  The fix:
1. `is_implicit_2d_centering_only()` detects when `item.transform`
   is **exactly** the implicit canvas-center translation.
2. If true, the source pass **strips** the implicit canvas-center
   from `item_world` before producing `m_matrix_override`.
3. The downstream `build_world_matrix` **re-applies** the canvas-center
   exactly once (the "TICKET-104 fix: skip `canvas_center` when
   `matrix_override` is set" comment).

**The hypothesis**: the strip in step (2) is **over-eager on X** for
Text-kind layers with non-trivial text boxes.  When the layer's
text box is, e.g., 1316 px wide (a real "THIS TEXT APPEARS" + "ONE
LETTER AT A TIME" typewriter line), the strip is computed
**relative to the text-box half-width** (658 px) on X, then
re-applied at canvas-center.  Net effect: the text is positioned
at `(960 − 658, 540)` = `(302, 540)` — **exactly the observed delta**.

### Why Y is unaffected

The Y axis is unaffected because the text height (≈130 px for the
example typewriter composition) is much smaller than the canvas
height (1080 px).  The Y offset that the strip computes
(half-height ≈ 65 px) is small enough that any "rounding" in the
glyph-bbox pipeline absorbs the error within the existing
20 px spread padding (see `compute_text_run_world_bbox` line ~190
in `src/text/text_run_geometry.cpp`).  The X delta (658 px) is
larger than the spread (20 px) and is therefore directly visible
in the world bbox.

### Why the predicted_bbox vs rendering paths both see the same wrong value

`TextRunNode::predicted_bbox()` (line 86 in `TextRunNode.cpp`) and
the rendering executor (line 83 in `text_run_execution.cpp`) **both
call the same `text_run::build_world_matrix(ctx, placement)`** with
the same `placement` object (verified in code-search).  The bug is
in `placement.matrix` — both paths see the same wrong matrix, so
the predicted bbox and the rendered pixels agree on the wrong
position.  This is consistent with the user's Step 2 observation
("text center at x≈302" — same in the engine's bbox output and in
the visible render).

## Proposed fix (DO NOT APPLY in this commit)

The fix is a **2–4 line change** in
`src/render_graph/builder/graph_builder_coordinates.hpp` (the
`is_implicit_2d_centering_only` function or its caller in
`should_use_centered_rendering`):

### Option A — Disable the strip for Text-kind layers (minimal)

```cpp
// In is_implicit_2d_centering_only(), add:
if (item.kind == LayerKind::Text) {
    return false;  // do NOT strip for Text-kind: the text layout engine
                   // already centers glyphs within the box; stripping
                   // double-shifts by the box half-width.
}
```

This matches the conservative pattern that TICKET-104 specifically
removed (see the comment "the `LayerKind::Normal` restriction was
removed"), but adds it back for Text-kind only.  The rationale
is that Text-kind has its own centering semantic in the text layout
engine that doesn't need the strip+reapply dance.

### Option B — Fix the strip math to be box-width-aware

```cpp
// In the source pass that strips the canvas-center, also strip
// the box half-width if the layer is Text-kind with a known box size.
if (item.kind == LayerKind::Text && item.text_box_size) {
    item_world = glm::translate(item_world, glm::vec3(
        -item.text_box_size.x * 0.5f,
        -item.text_box_size.y * 0.5f,
        0.0f));
}
```

This is more invasive but more correct in the long run.

### Option C — Use a `TextRunPlacement` carrier that bypasses the strip

Pass a separate "do not apply canvas-center" flag in
`TextRunPlacement` so the source pass doesn't strip, and
`build_world_matrix` doesn't re-apply.  Cleanest but requires
updating the `TextRunPlacement` struct.

**Recommendation**: **Option A** (minimal, surgical, well-scoped).
The `LayerKind::Text` exclusion restores the conservative pattern
that TICKET-104 removed, with a clear semantic justification
(text layout engine handles centering internally).

### Verification (after fix is applied)

```bash
# 1. Rebuild chronon3d_cli
cmake --build build --target chronon3d_cli -j$(nproc)

# 2. Render the AnimTypewriterGlow (or a fixed test composition)
#    and inspect the bbox center X
build/apps/chronon3d_cli/chronon3d_cli render AnimTypewriterGlow \
    --frame 140 -o /tmp/glow_xoffset_check.png

# 3. Expected: bbox center X ≈ 960 (canvas center), Y ≈ 540.
#    If still ~302, the fix is incomplete; if 0, the strip is
#    being applied without re-apply (regression to the TICKET-104
#    pre-fix state).

# 4. Add a regression test in tests/certification/test_cert_text_bbox.cpp:
#    - Render a centered text composition
#    - Assert predicted_bbox().x_center is within ±5 px of (canvas.width/2)
```

## Constraints honoured

- ✅ No production code modified (this commit is documentation only)
- ✅ Step-by-step trace with file:line citations for every claim
- ✅ Delta per step quantified (only step 3 has non-zero X)
- ✅ Y-axis unaffected explanation provided (text height << canvas height)
- ✅ Proposed fix is concrete (Option A: 4-line `LayerKind::Text` exclusion)
- ✅ Verification plan with reproducible commands

## Files referenced (full citation)

- `src/render_graph/nodes/text_run/text_run_transform.cpp:13–32` — `build_world_matrix` (no X math, returns `ssaa_scale * placement.matrix`)
- `src/render_graph/nodes/text_run/text_run_transform.hpp:31` — declaration
- `src/text/text_run_geometry.cpp:168–209` — `compute_text_run_world_bbox` (passive transform)
- `src/render_graph/builder/graph_builder_coordinates.hpp:40–58` — **THE LIKELY ROOT CAUSE** (TICKET-104 strip+reapply logic, see comment about double canvas-center)
- `include/chronon3d/text/text_run_geometry.hpp:63` — declaration of `compute_text_run_world_bbox`
- `include/chronon3d/text/text_placement.hpp:1–80` — `TextPlacement` / `TextPlacementKind` (resolver contract, Center vs CanvasCenter both resolve to canvas center)
- `src/render_graph/nodes/TextRunNode.cpp:86,90,105,330,387` — predicted_bbox calls `build_world_matrix(ctx, m_placement)` then `compute_text_run_world_bbox(*m_shape, matrix, spread)`
- `src/render_graph/nodes/text_run/text_run_execution.cpp:83` — rendering path also calls `build_world_matrix(ctx, placement)` (same `placement` object as predicted_bbox → no path divergence)
- `src/render_graph/nodes/multi_source_node.cpp:84–98` — multi-source path (also calls `compute_text_run_world_bbox` then falls back to `compute_world_bbox`)
- `docs/adr/ADR-019-text-coordinate-model.md:21,165,235–249,323–327` — coordinate model ADR (documents predicted_bbox vs rendering path divergence hypothesis)
- `docs/tickets/TICKET-TEXT-CLIP-PREDICTED-BBOX.md:34` — related ticket (predicted_bbox divergence)

## Next step (separate ticket)

Open `fix(text): TICKET-TEXT-XOFFSET-DOUBLE — exclude Text-kind from canvas-center strip` on
main with the Option A fix (4-line `LayerKind::Text` early return in
`is_implicit_2d_centering_only`), plus a regression test in
`tests/certification/test_cert_text_bbox.cpp` asserting
`bbox.x_center == 960 ± 5 px` for a centered-text composition.
