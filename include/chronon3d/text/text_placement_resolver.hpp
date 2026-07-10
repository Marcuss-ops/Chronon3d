#pragma once

// ============================================================================
// text_placement_resolver.hpp — Unified text placement resolver
//
// ADR-019 Decision 3: TextPlacement resolves the Box coordinate level —
// it determines where the text frame sits within the Layer or Canvas
// coordinate space.
//
// This resolver is the single entry point for converting high-level
// placement semantics (CanvasCenter, SafeAreaBottom, Absolute, etc.)
// into concrete Mat4 transforms and layout origins that the rendering
// pipeline consumes.
//
// The resolver produces:
//   - local_frame:    Box position in Canvas coords
//   - layer_matrix:   Layer transform (parent chain accumulated)
//   - world_matrix:   = layer_matrix × local_to_world(local_frame)
//   - layout_origin:  Origin for glyph layout (top-left of text frame
//                     after alignment adjustment)
//
// Used by:
//   - Future simple builder API (F2.B): .place(TextPlacement::CanvasCenter)
//   - Future resolve_text_placement() in F1.B
//   - Future TextDefinition (F2.A)
//
// Distinct from graph_builder_coordinates.hpp::resolve_text_run_placement()
// which operates at the graph-builder level with LayerGraphItem + RenderNode.
// This resolver operates at the authoring/semantic level with CanvasInfo +
// TextPlacement.
// ============================================================================

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>  // TextAnchor, TextAlign, VerticalAlign
#include <chronon3d/text/text_placement.hpp>      // TextPlacement, TextPlacementKind

namespace chronon3d {

// `TextPlacementKind` (the enum) and `TextPlacement` (the bundling struct)
// now live in `text_placement.hpp`.  See that header for the full 14-variant
// list and authoring rationale (Phase A.2 of the text V1 plan).
//
// Backward-compat note: the resolver functions previously took the enum
// `TextPlacement` + a separate `Vec2 offset` parameter.  After Phase A.2
// they take a single `TextPlacement` struct (kind + offset bundled).  This
// is a deliberate API consolidation (per AGENTS.md "Non duplicare…").

// ── CanvasInfo — canvas descriptor for placement resolution ───────────────
//
// Describes the canvas dimensions and safe area margins.  Used by
// resolve_text_placement() to compute placement positions.
//
// Safe area margins are in pixels from each edge.  Default values
// represent a typical 16:9 safe area (5% margin on each side).
struct CanvasInfo {
    f32 width{1920.0f};
    f32 height{1080.0f};
    f32 safe_margin_top{54.0f};     // 5% of 1080
    f32 safe_margin_bottom{54.0f};
    f32 safe_margin_left{96.0f};    // 5% of 1920
    f32 safe_margin_right{96.0f};
};

// ── ResolvedTextPlacement — output of the placement resolver ──────────────
//
// ADR-019 Decision 3: the resolved placement contains all coordinate
// information needed by the rendering pipeline.
//
// Coordinate levels (ADR-019 Decision 1):
//   - local_frame:    Box in Canvas coords (after placement resolution)
//   - layer_matrix:   Layer transform (parent chain)
//   - world_matrix:   = layer_matrix × local_to_world(local_frame)
//   - layout_origin:  Origin for glyph layout (top-left of text frame
//                     after alignment adjustment)
struct ResolvedTextPlacement {
    // Box position in Canvas coords (x, y, width, height).
    // Represents the top-left corner and size of the text frame.
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
    Vec2 layout_origin{0.0f, 0.0f};
};

// ── resolve_text_placement — the unified resolver ─────────────────────────
//
// Resolves text placement into concrete transforms and layout origins.
//
// This is the single entry point for converting high-level placement
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
//   placement:    High-level placement semantics (TextPlacement enum)
//   offset:       User-specified offset from the placement position (pixels)
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
