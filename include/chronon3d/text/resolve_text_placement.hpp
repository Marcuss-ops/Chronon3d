#pragma once

// ============================================================================
// resolve_text_placement.hpp — Canonical text placement resolver surface
//
// Phase A6 close-out (2026-07-10).
//
// ADR-019 Decision 3 establishes `resolve_text_placement()` as the SINGLE
// canonical surface for converting high-level placement semantics
// (CanvasCenter, SafeAreaBottom, Absolute, …) into the `ResolvedTextPlacement`
// the rendering pipeline consumes.  Resolution is Box-coordinate-level, as
// documented in `docs/adr/ADR-019-text-coordinate-model.md` §Decision 3.
//
// A previous co-resident `class TextPlacementResolver` (a thin shim that
// delegated every call to the free function) was REMOVED in Phase A6 per the
// AGENTS.md "Non duplicare…" rule.  The wrapper class was introduced in
// Phase A.3 to satisfy a literal-spec surface, but carried no state, no
// logic, and slightly indifferent delegation overhead.  The free function
// is the canonical surface; the class surface is permanently retired.
// Any future stateful extension (cache lease, prewarm hooks) will be added
// to the free-function surface directly, not resurrected as a parallel class.
//
// This header is the SINGLE include for callers wanting
// `CanvasInfo` / `ResolvedTextPlacement` / `resolve_text_placement` /
// `resolve_placement_origin` / `SafeAreaPreset`.  The now-retired wrapper
// header `text_placement_resolver.hpp` is BANNED from `include/chronon3d/`
// (see gate #23 in `tools/check_architecture_boundaries.sh`).
// ============================================================================

#include <chronon3d/math/glm_types.hpp>                      // Vec2 / Vec3 / Vec4 / Mat4
#include <chronon3d/scene/model/shape/shape.hpp>             // TextAnchor
#include <chronon3d/text/text_placement.hpp>                 // TextPlacement + TextPlacementKind + SafeAreaPreset

namespace chronon3d {

// ── CanvasInfo — canvas descriptor for placement resolution ───────────────
//
// Describes the canvas dimensions and safe area margins.  Consumed by
// `resolve_text_placement()` and `resolve_placement_origin()` to compute
// concrete Canvas-space pin points.  Safe area margins default to a
// typical 16:9 safe area (5% on each side, matching industry-standard
// title/action-safe zones).
struct CanvasInfo {
    f32 width{1920.0f};
    f32 height{1080.0f};
    f32 safe_margin_top{54.0f};     // 5% of 1080
    f32 safe_margin_bottom{54.0f};
    f32 safe_margin_left{96.0f};    // 5% of 1920
    f32 safe_margin_right{96.0f};

    // F3.B — SafeArea-aware factory: creates a CanvasInfo with margins
    // computed from a SafeAreaPreset and the given canvas dimensions.
    [[nodiscard]] static CanvasInfo with_safe_area(
        f32 width, f32 height, const SafeAreaPreset& preset);
};

// ── ResolvedTextPlacement — output of the placement resolver ──────────────
//
// ADR-019 Decision 3: the resolved placement contains all coordinate
// information needed by the rendering pipeline.  Phase A.3 also
// reinterprets the spec terms onto this single canonical struct (no
// parallel type introduced per AGENTS.md "Non duplicare...").  Mapping
// of the Phase A.3 spec terms to the actual fields (so users searching
// for `canvas_position` / `resolved_frame` / `resolved_anchor` can find
// the canonical name):
//
//   Phase A.3 spec term       |  ResolvedTextPlacement field
//   --------------------------+---------------------------------------------
//   Vec2  canvas_position     |  layout_origin  (Vec2; box top-left in Canvas)
//   TextFrame resolved_frame  |  local_frame    (Vec4 = x, y, width, height)
//   Anchor resolved_anchor    |  resolved_anchor (TextAnchor; input echo)
//
// The 2 additional fields (layer_matrix, world_matrix) are kept for the
// rendering pipeline's transform composition (ADR-019 Decision 1) — they
// are NOT part of the Phase A.3 spec terms but are essential for the
// downstream consumer path.
//
// Coordinate levels (ADR-019 Decision 1):
//   - local_frame:    Box in Canvas coords (after placement resolution)
//   - layer_matrix:   Layer transform (parent chain)
//   - world_matrix:   = layer_matrix × local_to_world(local_frame)
//   - layout_origin:  Origin for glyph layout (top-left of text frame
//                     after alignment adjustment)
//   - resolved_anchor: Echo of the input TextAnchor parameter (Phase A.3)
struct ResolvedTextPlacement {
    // Box position in Canvas coords (x, y, width, height).
    // Represents the top-left corner and size of the text frame.
    // Phase A.3 spec term: `resolved_frame` (TextFrame-style Vec4 packing).
    Vec4 local_frame{0.0f, 0.0f, 0.0f, 0.0f};

    // Layer transform (parent chain accumulated).
    // For top-level layers, this is identity.
    Mat4 layer_matrix{1.0f};

    // Final world matrix = layer_matrix × local_to_world(local_frame).
    // This is the matrix that transforms glyph positions from Box coords
    // to Canvas coords.  Consumed by TextRunPlacement.matrix.
    Mat4 world_matrix{1.0f};

    // Origin for glyph layout (top-left of text frame in Canvas coords).
    // After alignment adjustment (TextAlign + VerticalAlign).
    // Glyph positions are relative to this origin.
    // Phase A.3 spec term: `canvas_position` (Vec2 pin-point alias).
    Vec2 layout_origin{0.0f, 0.0f};

    // Phase A.3 — echo of the input `TextAnchor` parameter.
    // The resolver uses the anchor to compute `local_frame` (via
    // resolve_text_anchor's anchor_offset); until A.3 the input was
    // not echoed in the result struct (the value was used internally
    // for the offset computation but not stored).  Downstream
    // consumers (renderer, future builder API) re-derive layout from
    // this echo (e.g., re-anchor after a box resize without re-running
    // the full resolver).  Default is `TextAnchor::Center` to match
    // the free function's default parameter.
    TextAnchor resolved_anchor{TextAnchor::Center};
};

// ── resolve_text_placement — the canonical unified resolver ──────────────────
//
// Resolves text placement into concrete transforms and layout origins.
// This is the SINGLE entry point for converting high-level placement
// semantics into rendering-ready coordinates.  It handles:
//   1. Placement resolution (TextPlacement → box position)
//   2. Offset application (user-specified offset from placement)
//   3. Anchor resolution (TextAnchor → anchor point within box)
//   4. Alignment adjustment (TextAlign + VerticalAlign → layout_origin)
//   5. Matrix composition (layer_matrix × placement × anchor)
//
// The placement determines WHERE on the canvas the box's anchor point sits.
// The anchor determines WHICH POINT of the box aligns with that position.
//
// For TextPlacement::CanvasCenter + TextAnchor::Center:
//   pin_point = (960, 540)  (canvas center)
//   anchor_offset = (850, 180)  (box_size/2)
//   box_origin = pin_point - anchor_offset = (110, 360)
//
// For TextPlacement::CanvasCenter + TextAnchor::TopLeft:
//   pin_point = (960, 540)
//   anchor_offset = (0, 0)
//   box_origin = (960, 540)  (box starts at canvas center)
//
// Alignment (TextAlign, VerticalAlign) is a layout-engine concern that
// positions glyphs WITHIN the box — it is NOT handled by this resolver.
// The layout_origin is always the box's top-left corner.
//
// Parameters:
//   canvas:       Canvas dimensions and safe area margins
//   box_size:     Text frame size (width × height in pixels)
//   placement:    High-level placement semantics (text_placement.hpp)
//   anchor:       Which point of the box aligns with the placement position
//   layer_matrix: Parent layer transform (identity for top-level)
//
// Returns:
//   ResolvedTextPlacement with all coordinate information.
//
// Example:
//   CanvasInfo canvas{1920, 1080};
//   auto resolved = resolve_text_placement(
//       canvas, {1700, 360}, TextPlacement{TextPlacementKind::CanvasCenter});
//   // resolved.local_frame = {110, 360, 1700, 360}
//   // resolved.layout_origin = {110, 360}
//   // resolved.world_matrix = translate(110, 360, 0)
[[nodiscard]] ResolvedTextPlacement resolve_text_placement(
    const CanvasInfo& canvas,
    Vec2 box_size,
    TextPlacement placement,                          // kind + offset bundled
    TextAnchor anchor = TextAnchor::Center,
    const Mat4& layer_matrix = Mat4(1.0f)
);

// ── resolve_placement_origin — compute the PIN POINT for a placement ────
//
// Returns the "pin point" — the Canvas-space position where the box's
// ANCHOR POINT should sit.  This is NOT the box's top-left corner;
// the box top-left is derived by the caller (or by resolve_text_placement)
// as: box_top_left = pin_point - anchor_offset.
//
// For CanvasCenter:  pin = (canvas.width/2, canvas.height/2)
// For TopLeft:       pin = (safe_margin_left, safe_margin_top)
// For Absolute:      pin = placement.offset
//
// box_size is reserved for future placements that may need box geometry
// (e.g. auto-fit-aware placements).
//
// Parameters:
//   canvas:     Canvas dimensions and safe area margins
//   box_size:   Text frame size (reserved for future use)
//   placement:  High-level placement (kind + offset bundled struct)
[[nodiscard]] Vec2 resolve_placement_origin(
    const CanvasInfo& canvas,
    Vec2 box_size,
    TextPlacement placement
);

} // namespace chronon3d
