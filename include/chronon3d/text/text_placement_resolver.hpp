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

namespace chronon3d {

// ── TextPlacement — high-level placement semantics ────────────────────────
//
// Determines where the text box is positioned relative to the canvas or
// layer.  Each variant produces a different Vec2 origin in Canvas coords.
//
// Usage:
//   .place(TextPlacement::CanvasCenter)    // center of canvas
//   .place(TextPlacement::SafeAreaBottom)  // bottom safe area
//   .place(TextPlacement::Absolute({960, 540}))  // explicit position
//
enum class TextPlacement : u8 {
    CanvasCenter,       // Box center = Canvas center
    TopLeft,            // Box origin = Canvas (0, 0) + safe margin
    TopCenter,          // Box top-center = Canvas top-center + safe margin
    TopRight,           // Box top-right = Canvas top-right + safe margin
    CenterLeft,         // Box center-left = Canvas center-left
    CenterRight,        // Box center-right = Canvas center-right
    BottomLeft,         // Box bottom-left = Canvas bottom-left - safe margin
    BottomCenter,       // Box bottom-center = Canvas bottom-center - safe margin
    BottomRight,        // Box bottom-right = Canvas bottom-right - safe margin
    SafeAreaTop,        // Box top-center = Safe area top edge
    SafeAreaBottom,     // Box bottom-center = Safe area bottom edge
    Absolute,           // Box origin = explicit (x, y) in Canvas coords
};

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
//       canvas, {1700, 360}, TextPlacement::CanvasCenter);
//   // resolved.local_frame = {110, 360, 1700, 360}
//   // resolved.layout_origin = {110, 360}
//   // resolved.world_matrix = translate(110, 360, 0)
[[nodiscard]] ResolvedTextPlacement resolve_text_placement(
    const CanvasInfo& canvas,
    Vec2 box_size,
    TextPlacement placement,
    Vec2 offset = {},
    TextAnchor anchor = TextAnchor::Center,
    const Mat4& layer_matrix = Mat4(1.0f)
);

// ── resolve_placement_origin — compute the origin point for a placement ───
//
// Helper that computes just the Vec2 origin (top-left of the box) for a
// given placement, without building the full ResolvedTextPlacement.
// Useful for quick placement calculations without matrix composition.
//
// Parameters:
//   canvas:     Canvas dimensions and safe area margins
//   box_size:   Text frame size (width × height in pixels)
//   placement:  High-level placement semantics (TextPlacement enum)
//   offset:     User-specified offset from the placement position (pixels)
[[nodiscard]] Vec2 resolve_placement_origin(
    const CanvasInfo& canvas,
    Vec2 box_size,
    TextPlacement placement,
    Vec2 offset = {}
);

} // namespace chronon3d
