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
- `src/render_graph/builder/graph_builder_coordinates.hpp:112` — `source_space_world_matrix()` handles canvas-center stripping

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

## References

- `include/chronon3d/core/types/frame_context.hpp` — `FrameContext::local_frame` (timeline, not spatial)
- `include/chronon3d/scene/model/layer/layer.hpp:141` — `Layer::local_frame(Frame global_frame)`
- `include/chronon3d/scene/model/core/hierarchy_resolver.hpp:41` — `ResolvedNode::world_matrix`
- `include/chronon3d/scene/model/render/resolved_types.hpp:12` — `ResolvedLayer::world_matrix`
- `include/chronon3d/render_graph/builder/graph_builder.hpp:15` — `LayerGraphItem::world_matrix`
- `include/chronon3d/render_graph/builder/graph_builder_coordinates.hpp:112` — `source_space_world_matrix()`
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
