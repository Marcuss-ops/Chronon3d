# ADR-019 — Text Coordinate Model: 4-level Canvas / Layer / Box / Glyph

| Field | Value |
|---|---|
| **Status** | ✅ Documented + accepted |
| **Date** | 2026-07-10 |
| **Deciders** | Text pipeline architecture owner |
| **Tags** | `text`, `coordinates`, `placement`, `layout`, `bbox`, `predicted_bbox`, `Canvas`, `Layer`, `Box`, `Glyph`, `TextRunPlacement`, `TextDefinition`, `simplicity-action-plan` |
| **Related** | [ADR-015 — screen-space TRS invariant](./ADR-015-screen-space-trs-invariant.md) (2.5D projection output contract); [ADR-018 — auto-fit text](./ADR-018-auto-fit-text.md) (box-level layout); [ADR-014 — text golden coverage](./ADR-014-text-golden-coverage.md) (visual regression tests); `docs/TEXT_SIMPLICITY_ACTION_PLAN.md` §F1.A; `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md` §4 (canonical pipeline); `docs/FOLLOWUP_TICKETS.md` `TICKET-SIMPLICITY-COORDINATES`; `TICKET-TEXT-CLIP-PREDICTED-BBOX` (predicted_bbox divergence bug) |

---

## Context and scope

Chronon3D's text pipeline processes text through multiple coordinate spaces, but these spaces are **implicit** — no single document defines what they are, how they relate, or where the boundaries lie. This causes several concrete problems:

1. **`predicted_bbox` divergence** (`TICKET-TEXT-CLIP-PREDICTED-BBOX`): The predicted bounding box used for tile pruning can diverge from the actual rendered bbox because the compositor and the predicted_bbox computation use different assumptions about which coordinate space they operate in. The result is a visible "white stripe" artifact (19px sliver) where text is pruned but should be visible.

2. **Ambiguous `position` semantics**: `TextSpec.position` can mean canvas coordinates, layer-local coordinates, or box-relative coordinates depending on which code path consumes it. The `centered_text()` helper hides this ambiguity by computing multiple coordinates at once.

3. **Transform application uncertainty**: When a text node has a parent layer with a transform, it's unclear whether `anchor`, `alignment`, and `offset` are computed before or after the parent transform. The `build_world_matrix()` function in `src/render_graph/nodes/text_run/text_run_transform.cpp` composes these, but the contract is undocumented.

4. **Test fragility**: Tests like `text_clip_bounds.cpp` and `text_completeness.cpp` must manually verify that predicted_bbox contains the world_ink_bbox. A formal coordinate model would make this a structural invariant rather than a per-test assertion.

This ADR formalizes the 4-level coordinate model that already exists implicitly in the codebase, making it explicit so that:
- Every function that computes or consumes a bbox can declare which coordinate space it operates in
- The `predicted_bbox` vs `world_ink_bbox` divergence can be fixed structurally (not per-case)
- The new `TextDefinition` (F2.A in the simplicity action plan) and `resolve_text_placement()` (F1.B) can use a shared vocabulary

---

## Decision 1 — Four coordinate levels, each with a clear owner

### Spec

Every text element exists in 4 coordinate levels, applied in order:

```text
Canvas → Layer → Box → Glyph
```

| Level | Coordinate space | Origin | Unit | Owner function |
|---|---|---|---|---|
| **Canvas** | Global pixel space (e.g. 1920×1080) | Top-left corner of the frame | Pixels | Frame input / CLI |
| **Layer** | Post-parent-transform space | Layer's anchor point (typically center) | Pixels | `world_matrix` / `project_layer_2_5d()` |
| **Box** | Text frame local space | Top-left of the text frame | Pixels | `TextFrame` / `TextPlacement` |
| **Glyph** | Per-glyph positioned space | Glyph origin (baseline + advance) | Pixels | `PlacedGlyphRun` / `GlyphInstanceState` |

### Transform chain

```text
Canvas_point = Layer_matrix × Box_offset × Glyph_position
```

Where:
- `Layer_matrix` = `world_matrix` from `layer_hierarchy.cpp` (parent transforms accumulated)
- `Box_offset` = placement-resolved origin within the layer (e.g. center of canvas for `TextPlacement::CanvasCenter`)
- `Glyph_position` = layout-computed per-glyph position (baseline + advance + animator offset)

### Source anchors

| Level | Primary source | Secondary source |
|---|---|---|
| Canvas | `RenderGraphContext::frame_input.width/height` | `apps/chronon3d_cli/commands/render/command_still.cpp` |
| Layer | `src/scene/model/layer_hierarchy.cpp` `ResolvedNode::world_matrix` | `src/render_graph/layer_resolver.cpp` line 64 |
| Box | `include/chronon3d/text/text_document.hpp` `TextFrame` | `src/render_graph/nodes/text_run/text_run_transform.cpp` `build_world_matrix()` |
| Glyph | `include/chronon3d/backends/text/text_layout_types.hpp` `TextLayoutRun` | `src/text/text_run_geometry.cpp` `compute_text_run_visual_bounds()` |

### Test lock (planned)

A test that verifies the transform chain end-to-end:

```text
Given: Canvas 1920×1080, Layer at center (960, 540), Box 1700×360 centered,
       Glyph "A" at box origin (0, 0)

Expected:
  Glyph canvas position = Layer_matrix × Box_offset × (0, 0)
                        = translate(960, 540) × translate(-850, -180) × (0, 0)
                        = (110, 360)  ← top-left of the text box in canvas coords
```

### Failure mode (if violated)

- If Glyph coordinates are interpreted as Canvas coordinates, text renders at the wrong position when the layer has a parent transform (e.g. a precomp or group layer).
- If Box coordinates are interpreted as Layer coordinates, `TextPlacement::CanvasCenter` places text at the layer's center instead of the canvas center.

---

## Decision 2 — Every bbox-producing function declares its coordinate level

### Spec

Every function that returns a `raster::BBox` MUST document which coordinate level the bbox is in, using this naming convention:

| BBox name | Coordinate level | Used by |
|---|---|---|
| `local_bbox` | **Glyph** (per-glyph local space, before layer transform) | `compute_text_run_visual_bounds()` |
| `world_bbox` | **Canvas** (after all transforms applied) | `compute_text_run_world_bbox()` |
| `predicted_bbox` | **Canvas** (must contain `world_bbox`) | `TextRunNode::predicted_bbox()` |
| `alpha_bbox` | **Canvas** (actual rendered pixel bounds) | Post-rasterization measurement |

### Invariant

```text
local_bbox ⊆ world_bbox ⊆ predicted_bbox
alpha_bbox ⊆ predicted_bbox
```

Where `⊆` means "is contained within" (all corners of the inner bbox are inside the outer bbox).

### Source anchors

| Function | File | Coordinate level |
|---|---|---|
| `compute_text_run_visual_bounds()` | `src/text/text_run_geometry.cpp:54` | Glyph (local) |
| `compute_text_run_world_bbox()` | `src/text/text_run_geometry.cpp:168` | Canvas (world) |
| `TextRunNode::predicted_bbox()` | `src/render_graph/nodes/TextRunNode.cpp:78` | Canvas (predicted) |
| `MultiSourceNode::predicted_bbox()` | `src/render_graph/nodes/multi_source_node.cpp:25` | Canvas (predicted) |
| `compute_world_bbox()` | `src/backends/software/rasterizers/shape_rasterizer.cpp:35` | Canvas (world) |

### Test lock

Existing tests that verify the containment invariant:

- `tests/text_golden/text_clip/text_clip_bounds.cpp` — Clip 01/02/03: verifies `predicted_bbox` contains `world_ink_bbox`
- `tests/text_golden/text_clip/text_completeness.cpp` — 15 TEST_CASEs: verifies no glyph is clipped
- `tests/architecture/test_protected_core_contracts.cpp:241` — `CoreContract: TextRunNode predicted_bbox is non-empty`

### Failure mode (if violated)

This is the **root cause of TICKET-TEXT-CLIP-PREDICTED-BBOX**: when `predicted_bbox` does not contain `world_bbox`, the tile pruning system clips visible text, producing the 19px white stripe artifact. The fix is to ensure the containment invariant holds at all coordinate levels.

---

## Decision 3 — TextPlacement resolves the Box level

### Spec

`TextPlacement` (to be introduced in F2.B / `TICKET-SIMPLICITY-BUILDER`) resolves the **Box** coordinate level — it determines where the text frame sits within the Layer or Canvas coordinate space.

```cpp
enum class TextPlacement {
    CanvasCenter,      // Box center = Canvas center
    CanvasTopLeft,     // Box origin = Canvas (0, 0)
    SafeAreaBottom,    // Box bottom = Canvas safe area bottom margin
    Absolute,          // Box origin = explicit (x, y) in Canvas coords
    // ... more variants
};
```

The resolution produces a `Vec2` origin in the **Box** coordinate space:

```cpp
struct ResolvedTextPlacement {
    Rect local_frame;      // Box in Canvas coords (after placement resolution)
    Mat4 layer_matrix;     // Layer transform (parent chain)
    Mat4 world_matrix;     // = layer_matrix × local_to_world(local_frame)
    Vec2 layout_origin;    // Origin for glyph layout (top-left of text frame)
};
```

### Source anchors (current)

- `src/render_graph/nodes/text_run/text_run_transform.cpp:13` — `build_world_matrix()` currently composes the text run's world matrix from context + placement
- `include/chronon3d/render_graph/nodes/detail/projection_helpers.hpp:70` — `build_projected_draw_matrix()` composes `canvas_center * ssaa_scale * proj.transform.to_mat4()`
- `src/render_graph/builder/graph_builder_coordinates.hpp:112` — `source_space_world_matrix()` handles canvas-center stripping <!-- drift-allow: stale-ref -->

### Test lock (planned)

A test for each `TextPlacement` variant that verifies the resolved `layout_origin` against expected Canvas coordinates for a 1920×1080 canvas.

### Failure mode (if violated)

If `TextPlacement` resolves in the wrong coordinate space, `.place(TextPlacement::CanvasCenter)` on a nested layer (child of a precomp) would place text at the precomp's center instead of the canvas center.

---

## Decision 4 — Glyph coordinates are relative to the text frame origin

### Spec

Per-glyph positions from the layout engine (`PlacedGlyphRun::glyphs[i].position`) are in **Glyph** coordinates, which are relative to the text frame's `layout_origin` (top-left of the text box, after alignment).

The transform from Glyph to Canvas is:

```text
Canvas_pos = world_matrix × translate(layout_origin) × Glyph_pos
```

Where:
- `world_matrix` includes the Layer transform + Box placement
- `layout_origin` includes alignment offset (center, right, etc.)
- `Glyph_pos` is the per-glyph position from `TextRunLayout`

### Source anchors

- `src/text/text_run_geometry.cpp:54` — `compute_text_run_visual_bounds()` accumulates per-glyph bboxes in Glyph coordinates
- `src/text/text_run_geometry.cpp:168` — `compute_text_run_world_bbox()` transforms from Glyph to Canvas via `model` matrix
- `include/chronon3d/text/text_run_layout.hpp` — `PlacedGlyphRun` holds per-glyph positions

### Numerical example (1920×1080 canvas)

```text
Canvas: 1920×1080
Layer:  translate(960, 540) — centered at canvas origin
Box:    1700×360, placed at CanvasCenter
        → layout_origin = (960-850, 540-180) = (110, 360) in Canvas coords
Glyph:  "H" at Glyph_pos = (0, 0) (first glyph, baseline at origin)
        → Canvas_pos = translate(960,540) × translate(-850,-180) × (0, 0)
                     = (110, 360)

With alignment offset (Center, Middle):
        → layout_origin += ((1700 - text_width)/2, (360 - text_height)/2)
        → Canvas_pos adjusted accordingly
```

### Test lock

- `tests/text_golden/text_placement/text_placement_golden.cpp` — verifies visual placement across aspect ratios
- `tests/text_golden/text_clip/text_clip_bounds.cpp` — Clip 01/02/03 verifies glyph positions are within predicted_bbox

### Failure mode (if violated)

If Glyph coordinates are not relative to the text frame origin but instead relative to the canvas origin, then `TextPlacement::Absolute({960, 540})` would place glyphs at (960+x, 540+y) instead of (960, 540) + glyph_offset — doubling the placement offset.

---

## Decision 5 — predicted_bbox MUST use the same matrix chain as rendering

### Spec

`predicted_bbox` and the actual rendering MUST use the same matrix chain. This is the structural fix for `TICKET-TEXT-CLIP-PREDICTED-BBOX`.

Currently, `TextRunNode::predicted_bbox()` calls `build_world_matrix()` and then `compute_text_run_world_bbox()`, while the rendering path (`text_run_execution.cpp`) also calls `build_world_matrix()`. The divergence happens when:

1. The `build_world_matrix()` call in `predicted_bbox` uses a different `TextRunPlacement` than the rendering path
2. The `compute_text_run_world_bbox()` function applies different spread/padding than the actual rasterizer
3. The 2.5D projection (`project_layer_2_5d`) produces different matrices for predicted_bbox vs rendering (see ADR-015 Decision 3)

### Resolution

Both paths MUST call the same `build_world_matrix(ctx, placement)` with the same `placement` value, and `compute_text_run_world_bbox()` MUST use the same spread value as the rasterizer.

### Source anchors

- `src/render_graph/nodes/TextRunNode.cpp:86` — predicted_bbox calls `build_world_matrix(ctx, m_placement)`
- `src/render_graph/nodes/text_run/text_run_execution.cpp:83` — rendering calls `build_world_matrix(ctx, placement)`
- `src/text/text_run_geometry.cpp:168` — `compute_text_run_world_bbox()` adds spread

### Test lock

```text
TEST: predicted_bbox_contains_world_bbox
  Given: TextRunNode with text "HELLO", layer at center, frame 0
  When:  predicted_bbox(ctx) and actual render
  Then:  predicted_bbox ⊇ alpha_bbox (containment holds)
```

This test should be added to `tests/text_golden/text_clip/` as a structural invariant.

### Failure mode (if violated)

This is the **existing bug** tracked by `TICKET-TEXT-CLIP-PREDICTED-BBOX`: when the matrix chains diverge, `predicted_bbox` is smaller than `alpha_bbox`, and tile pruning clips visible text. The fix is ensuring both paths use the same matrix chain — this ADR formalizes the requirement.

---

## Consequences

### Positive

- **Single vocabulary** for all coordinate-related discussions. "Canvas coords" now has a precise definition (global pixel space, top-left origin).
- **Structural fix path for TICKET-TEXT-CLIP-PREDICTED-BBOX**: Decision 5 makes the containment invariant a formal requirement, not a per-test assertion.
- **Foundation for TextDefinition (F2.A)**: The new `TextDefinition` can declare which coordinate level each field operates in.
- **Foundation for resolve_text_placement (F1.B)**: The resolver can explicitly transform between Canvas → Layer → Box → Glyph levels.
- **Test simplification**: Tests can assert "this bbox is in Canvas coords" rather than manually verifying containment.

### Negative / Migration cost

- **Doc-only addition**: No source-code changes. The coordinate model already exists implicitly.
- **Naming convention adoption**: Existing functions must be audited to ensure their bbox return values follow the naming convention (`local_bbox`, `world_bbox`, `predicted_bbox`, `alpha_bbox`). This is a gradual migration, not a breaking change.
- **No structural enforcement yet**: The containment invariant (Decision 2) is enforced by tests, not by type system. A future `BBox<CoordLevel>` typed bbox could enforce it at compile time, but that's out of scope.

### Neutral

- No new public API symbols.
- No new dependencies.
- No changes to existing behavior.

---

## Alternatives considered

### A. Typed coordinate spaces (compile-time enforcement)

Considered using template parameter `BBox<CanvasCoord>` vs `BBox<GlyphCoord>` to prevent mixing coordinate spaces at compile time. Rejected — too invasive for the current codebase (every `raster::BBox` usage would need to be updated). The naming convention + test lock is sufficient for now.

### B. Single unified coordinate space (all in Canvas)

Considered converting everything to Canvas coordinates immediately. Rejected — the Glyph coordinate level is essential for per-glyph animation (selectors, animators). Converting to Canvas too early would make per-glyph animation impossible.

### C. 3 levels instead of 4 (merge Box into Layer)

Considered treating Box as part of the Layer level. Rejected — the Box level is distinct because it handles text-specific concerns (alignment, overflow, wrap, auto-fit) that don't apply to generic layers. ADR-018 (auto-fit) operates at the Box level specifically.

### D. 5 levels (add Frame level for sequences)

Considered adding a Frame level between Canvas and Layer for sequence-local coordinates. Rejected — `FrameContext::local_frame` already handles this at the timeline level, not the spatial coordinate level. Spatial coordinates don't change with frame.

---

## §6 — Numerical Examples (1920×1080 canonical canvas)

> **Origin:** this section was added in §2 follow-up (2026-07-10, post-F1.A). It collects
> a single reproducible numerical lattice (canvas 1920×1080, default 5% safe-area
> margins = top/bottom 54, left/right 96) so the decisions above can be inspected
> end-to-end with one screen of numbers. Tests in
> [`tests/text/test_text_placement_resolver.cpp`](../../tests/text/test_text_placement_resolver.cpp)
> lock every numerical claim below. **No fourth coordinate level is added** —
> this section is a worked example, not a model extension.

### §6.1 — Placement (TextPlacementKind → pin point in Canvas coords)

Canvas 1920×1080, default safe margins `96/96/54/54` (Left/Right/Top/Bottom),
box 1700×360, no offset, anchor `TextAnchor::Center`.

| Placement Kind              | Pin (Canvas px) | Formula                                  | Resolved origin (anchor = Center, box_top_left = pin − box/2) |
|-----------------------------|-----------------|------------------------------------------|--------------------------------------------------------------|
| `CanvasCenter`              | `(960, 540)`    | `(width/2, height/2)`                    | `(110, 360)`                                                 |
| `TopLeft`                   | `(96, 54)`      | `(safe_margin_left, safe_margin_top)`    | `(-754, -126)` (off-screen; box's center at the safe margin)|
| `TopCenter`                 | `(960, 54)`     | `(width/2, safe_margin_top)`             | `(110, -126)` (off-screen below 0; box extends down)        |
| `TopRight`                  | `(1824, 54)`    | `(width - safe_margin_right, top)`       | `(974, -126)`                                                |
| `CenterLeft`/`CenterRight`  | `(96/1824, 540)`| `(margin, height/2)` / `(width-margin, h/2)`| `(-704, 360)` / `(974, 360)`                                 |
| `BottomLeft/Center/Right`   | `(96/960/1824, 1026)` | `(margin, height - bottom_margin)` | top-left `(-704, 846)`, `(110, 846)`, `(974, 846)`           |
| `SafeAreaTop`/`SafeAreaBottom` | `(960, 54)/(960, 1026)` | same as TopCenter/BottomCenter | `(110, -126)` / `(110, 846)`                               |
| `SafeAreaCenter`            | `(960, 540)`    | `((sl + (w-sr))/2, (st + (h-sb))/2)`       | `(110, 360)` (same as CanvasCenter for symmetric 5%-margins)|
| `Absolute({x,y})`           | `(x, y)`        | `offset` itself (additive offset ignored)| origin = `(x - 850, y - 180)` for box 1700x360                |

### §6.2 — Offset (placement.offset additivity)

Same canvas 1920×1080 + box 800x200 (small for legibility).

| Placement        | offset           | Pin                       | Note                                              |
|------------------|------------------|---------------------------|---------------------------------------------------|
| `TopLeft`        | `{0, 0}`         | `(96, 54)`                | Default — pin = safe-margin corner                 |
| `TopLeft`        | `{10, 20}`       | `(106, 74)`               | Additive: `(96 + 10, 54 + 20)`                     |
| `TopLeft`        | `{1824, 1026}`   | `(1920, 1080)`            | Pushes pin past the canvas — pin = origin + offset |
| `CanvasCenter`   | `{-200, 100}`    | `(760, 640)`              | Negative offset shifts pin left/up                |
| `Absolute({500,300})` | _ignored_  | `(500, 300)`              | `Absolute` is **not** additive (per Decision 3 § `Absolute` spec) |

**Key contract:** pin = `placement_resolved_kind(canvas)` + `placement.offset`,
_except_ for `Absolute` (where `offset` IS the pin and no kind math is applied).

### §6.3 — Alignment (TextAlign + VerticalAlign — layout-engine concern)

The placement resolver returns `layout_origin` = box top-left in Canvas
coords (`(110, 360)` for the §6.1 example). The layout engine then positions
each glyph within the box according to `TextAlign` (horizontal) and
`VerticalAlign` (vertical).

For box `(110, 360, 1700, 360)` and a notional text run of width `W`
and height `H`:

| align        | vertical_align | Glyph block origin (Box-local)                          |
|--------------|----------------|---------------------------------------------------------|
| `Left`       | `Top`          | `(0, 0)`                                                |
| `Center`     | `Middle`       | `((1700 - W)/2, (360 - H)/2)`                            |
| `Right`      | `Bottom`       | `(1700 - W, 360 - H)`                                    |
| `Left`       | `Middle`       | `(0, (360 - H)/2)`                                      |
| `Center`     | `Top`          | `((1700 - W)/2, 0)`                                     |
| `Justify`*   | _n/a_          | *Per-line left-to-right flush, no block offset*          |

> (*) `Justify` is a future extension tracked by `TICKET-FASE4-LAYOUT`
> (V0.2 §Fase 4 cluster); not yet implemented.

**Key contract:** the resolver returns the box origin; alignment is a
layout-engine concern handled *inside* the box. Tests in
`tests/text/test_text_placement_resolver.cpp::TextPlacement: ... + Anchor::...`
indirectly confirm by locking box origin (and so the layout engine's
working space).

### §6.4 — Rotation (layer_matrix rotation, around box center)

The placement resolver composes `world_matrix = layer_matrix * T(origin)`.
Rotation enters via `layer_matrix`; it rotates the entire text frame around
the layer origin (typically the canvas center `(960, 540)` for a top-level
layer) by the angle theta = rad(`rotation_z`).

Setup: canvas 1920x1080, box 1700x360, `CanvasCenter`, anchor `Center`, origin
`(110, 360)`. Apply `layer_matrix = rotate_z(15°)`:

| Element                                  | Pre-rotation            | Post-rotation (15° around `(960, 540)`)               |
|------------------------------------------|-------------------------|--------------------------------------------------------|
| Box top-left (Box-local `(0, 0)`)        | Canvas `(110, 360)`     | `(110-960, 360-540)` rotated -> `(909.1, 511.4)`     |
| Box top-right (Box-local `(1700, 0)`)    | Canvas `(1810, 360)`    | `(1810-960, 360-540)` rotated -> `(1410.6, 218.6)`    |
| Box bottom-left (Box-local `(0, 360)`)   | Canvas `(110, 720)`     | `(110-960, 720-540)` rotated -> `(789.1, 754.4)`      |
| Box center                                | Canvas `(960, 540)`    | `(960, 540)` (rotation pivot, invariant)              |

**Key contract:** rotation is applied _after_ placement resolution (the box
origin is computed in Canvas coords, then the entire frame rotates around
the layer origin). For 90° the rotation matrix degenerates to a swap;
numerical lock is at 15° to avoid the degenerate case. Tests in
`tests/text/test_text_placement_resolver.cpp` extend rotation coverage via
`layer_matrix` rotation parameter — see `TextPlacement rotate_layer_matrix`
case (planned companion test).

### §6.5 — Anchor (TextAnchor → anchor_offset within the box)

Same canvas 1920x1080, box 1700x360, `CanvasCenter` placement (pin `(960, 540)`):

| Anchor             | anchor_offset (Box-local) | box_top_left (Canvas)                | behavioral note                          |
|--------------------|---------------------------|-------------------------------------|------------------------------------------|
| `TopLeft`          | `(0, 0)`                  | `(960, 540)`                         | Box's top-left at canvas center          |
| `TopCenter`        | `(850, 0)`                | `(110, 540)`                         | Box's top-center at canvas center        |
| `Center`           | `(850, 180)`              | `(110, 360)`                         | **Default** — box's center at canvas center |
| `CenterRight`      | `(1700, 180)`             | `(-740, 360)`                        | Box's right-center at canvas center (off-screen) |
| `BottomLeft`       | `(0, 360)`                | `(960, 180)`                         | Box's bottom-left at canvas center       |
| `BottomCenter`     | `(850, 360)`              | `(110, 180)`                         | Box's bottom-center at canvas center     |
| `BaselineLeft`     | `(0, baseline_y)`         | `(960, 540 - baseline_y)`            | Baseline at canvas center (y depends on font metrics) |
| `BaselineCenter`   | `(850, baseline_y)`       | `(110, 540 - baseline_y)`            | Baseline-center at canvas center         |

**Key contract:** anchor controls WHICH POINT OF THE BOX aligns with the
pin point. `Center` is the default and matches the canonical "centered
title" use case (Hero example in §Obiettivo finale of the simplicity
plan). `Baseline*` variants depend on font metrics (ascent); the layout
engine resolves them at shape time, not at the resolver's call site.

### §6.6 — Putting it together (Hero example)

Layer-text authoring target from §Obiettivo finale (this is the
end-to-end pin point for the 5 sections above + §1, §3, §4, §6, §7-§13):

```
layer.text("title")
  .content("PULSE GLOW")
  .font("assets/fonts/Inter-Bold.ttf", 230.0f)
  .frame({1700.0f, 360.0f})
  .place(TextPlacement::CanvasCenter)
  .opacity(interpolate(ctx.frame, {0, 15}, {0.4f, 0.85f}))
  .commit();
```

On Canvas 1920x1080, default safe-area 5%, font Inter-Bold 230 px:

| Property              | Value (after resolver)                                               |
|-----------------------|----------------------------------------------------------------------|
| Pin point             | `(960, 540)` (canvas center)                                         |
| Anchor                | `Center` (default)                                                   |
| Anchor offset         | `(850, 180)`                                                         |
| Box top-left (Canvas) | `(110, 360)`                                                         |
| Box extent (Canvas)   | `(110, 360)` -> `(1810, 720)`                                        |
| Layout origin         | `(110, 360)`                                                         |
| world_matrix          | `translate(110, 360)` (top-level layer, identity parent)             |
| Predicted canvas bbox | contains `(110, 360, 1700, 360)` (containment with world_ink_bbox per Decision 5)|
| Opacity @ frame 0     | `0.4f` (start of `interpolate` range)                                |
| Opacity @ frame 15    | `0.85f` (end of `interpolate` range)                                 |

This is the single worked example each follow-up commit (placement,
offset, alignment, rotate, anchor) MUST be cross-checked against.

---

## Cross-references

- `docs/ROADMAP.md` §M1.8 §2 — backward link from the §1A row of the M1.8 table
- `include/chronon3d/text/text_definition.hpp` — top-of-file docblock quote of the §6 lattice

---

## References

- `include/chronon3d/core/types/frame_context.hpp` — `FrameContext::local_frame` (timeline, not spatial)
- `include/chronon3d/scene/model/layer/layer.hpp:141` — `Layer::local_frame(Frame global_frame)`
- `include/chronon3d/scene/model/core/hierarchy_resolver.hpp:41` — `ResolvedNode::world_matrix`
- `include/chronon3d/scene/model/render/resolved_types.hpp:12` — `ResolvedLayer::world_matrix`
- `include/chronon3d/render_graph/builder/graph_builder.hpp:15` — `LayerGraphItem::world_matrix`
- `include/chronon3d/render_graph/builder/graph_builder_coordinates.hpp:112` — `source_space_world_matrix()` <!-- drift-allow: stale-ref -->
- `include/chronon3d/render_graph/nodes/detail/projection_helpers.hpp:70` — `build_projected_draw_matrix()`
- `include/chronon3d/math/camera_2_5d_projection.hpp:38` — `ProjectedLayer2_5D` (ADR-015)
- `include/chronon3d/text/text_run_geometry.hpp:52` — `compute_text_run_visual_bounds()`
- `include/chronon3d/text/text_run_geometry.hpp:63` — `compute_text_run_world_bbox()`
- `src/render_graph/nodes/text_run/text_run_transform.cpp:13` — `build_world_matrix()`
- `src/render_graph/nodes/TextRunNode.cpp:78` — `TextRunNode::predicted_bbox()`
- `src/render_graph/nodes/multi_source_node.cpp:25` — `MultiSourceNode::predicted_bbox()`
- `src/backends/software/rasterizers/shape_rasterizer.cpp:35` — `compute_world_bbox()`
- `tests/text_golden/text_clip/text_clip_bounds.cpp` — Clip 01/02/03 regression locks
- `tests/text_golden/text_clip/text_completeness.cpp` — 15 TEST_CASEs visibility contract
- `tests/architecture/test_protected_core_contracts.cpp:241` — predicted_bbox non-empty contract
- `docs/TEXT_SIMPLICITY_ACTION_PLAN.md` §F1.A — this ADR's origin
- `docs/FOLLOWUP_TICKETS.md` `TICKET-SIMPLICITY-COORDINATES` — tracking ticket
- `docs/FOLLOWUP_TICKETS.md` `TICKET-TEXT-CLIP-PREDICTED-BBOX` — predicted_bbox divergence bug
- `tools/wrap_push.sh` — GATE-MNT-01 push-side wrapper
- `tools/check_doc_sync.sh` — gate #7

## Historical Notes

> Cronaca archiviate dai public headers `text_definition.hpp` e
`layer_builder.hpp` durante lo slim I4-PHASE-1. Ciascuna sezione
specifica la source-file + i blocchi rimossi + il payload originale
(per future regression lookups, ADR-traceback, o rot-rotation).

### §A.1 text_definition.hpp (I4-SLIM)

Original cronaca archived per Cat-3 anti-duplication. Full content of the
three slimmed comment blocks (T1/T2/T3) preserved below for traceback.

**T1** — top-of-file F2.A lifecycle mega-block (baseline lines 3-75, 73 lines)
- `TextDefinition` is the SINGLE canonical representation for text authoring.
  Every text API (layer.text(), text_run(), centered_text(), presets) produces
  a TextDefinition. No duplicate representations for font size, position,
  anchor, alignment, box, overflow, color, or stroke.
- LIFECYCLE progression:
  - F2.A: reuse TextContent from builder_params.hpp + TextStyle + TextFrame
    with real fields mapped from TextSpec.
  - Phase A.3: TextAnimation fields (animators + selectors + run-control
    + Frame envelope). from_text_run_spec now routes the 6 spec-only fields
    into TextAnimation (replaces prior (void)silence).
  - F2.D: to_text_run_spec closes the adapter gap. The 6 spec-only fields
    (direction/language/script/animators/selectors) are now carried back to
    a TextRunSpec from a TextDefinition. ADDITIONAL LOSSY DROP (per-run
    Frame envelope): TextAnimation.start_delay + .duration are NOT
    representable in TextRunSpec and are silently dropped during
    to_text_run_spec. Roundtrip TextDefinition → TextRunSpec → TextDefinition
    re-initialises the Frame envelope to Frame{0}. Documented tested behaviour.
  - F3.D: LayerBuilder overloads (text(name, TextDefinition) in
    commands/shape_commands.cpp + text_run(name, TextDefinition) in
    commands/layer_builder_compile.cpp) now route via to_text_run_spec()
    instead of the F2.C lossy from_text_definition() path. End-to-end
    lossless for 17 helper-site call sites. F3.D also ADDS the symmetric
    `text(name, TextRunSpec)` overload (Frame envelope drop identical).
  - Phase B: to_text_document(const TextDefinition&) lowers this DTO into
    the canonical TextDocument pipeline model consumed by compile_text_layout().
- DO NOT USE TextDefinition IN THE RUNTIME PIPELINE (TextDocument is the
  canonical pipeline model; TextDefinition = authoring surface only).
- Per AGENTS.md "Non duplicare": TextDefinition complements (does not
  duplicate) TextDocument.
- ANTI-DUPLICATION: TextDefinition is now the SOLE canonical authoring DTO.

**T2** — TextAnimation struct contract comment (baseline lines 217-241, 25 lines)
- Per-run animation contract mirroring TextRunSpec (builder_params.hpp).
- Fields: animators (TextAnimatorSpec[]), selectors (GlyphSelectorSpec[]),
  direction (TextDirection, default Auto), language (BCP-47, empty=auto),
  script (OpenType tag, 0=auto, 0x4C61746E Latin, 0x41726162 Arabic),
  cache_layout (hint), start_delay (Frame{0}=immediate, GLOBAL envelope),
  duration (Frame{0}=use per-animator timeline, GLOBAL envelope length).
- Sibling naming: `TextAnimation` — verified no collision (TextAnimationSpec
  doesn't exist; animation types live under chronon3d::text::animation).

**T3** — to_text_run_spec reverse adapter doc-comment (baseline lines 305-323, 19 lines)
- F2.D reverse adapter from TextDefinition → TextRunSpec (carries animators,
  selectors, run-control fields, cache_layout — fields NOT in TextSpec).
- LOSSY DROP (documented): TextRunSpec does NOT represent the Frame envelope
  (TextAnimation.start_delay + .duration); roundtrip → Frame{0}. Documented
  tested behaviour (test_text_definition.cpp group 19).
- Base fields (style, frame, paragraph) reuse from_text_definition() so the
  two reverse adapters (`from_text_definition` for TextSpec and
  `to_text_run_spec` for TextRunSpec) cannot drift out of sync — per
  Phase B risk register "Duplicating the Base TextSpec Conversion".
- Parallel naming pattern to `to_text_document` (Phase B) — both
  lowerers consume TextDefinition into a downstream model. Name chosen
  over the user-suggested `from_run_spec_definition` for symmetry with
  `to_text_document`.
