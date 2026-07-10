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
// Phase A.3 of the text V1 plan also reinterprets the spec terms onto this
// single canonical struct (no parallel type introduced per AGENTS.md
// "Non duplicare...").  Mapping of the Phase A.3 spec terms to the actual
// fields (so users searching for `canvas_position` / `resolved_frame` /
// `resolved_anchor` can find the canonical name):
//
//   Phase A.3 spec term       |  ResolvedTextPlacement field
//   --------------------------+---------------------------------------------
//   Vec2  canvas_position     |  layout_origin  (Vec2; box top-left in Canvas)
//   TextFrame resolved_frame  |  local_frame    (Vec4 = x, y, width, height)
//   Anchor resolved_anchor   |  resolved_anchor (TextAnchor; input echo)
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

// ── TextPlacementResolver — class-based resolver surface (Phase A.3) ─────
//
// Phase A.3 spec asks for a `TextPlacementResolver` class wrapping the
// resolver.  The free function `resolve_text_placement` remains the
// canonical implementation (per the AGENTS.md "Non duplicare..." rule
// and the Option C reinterpretation from the thinker's verdict); this
// class is a thin literal-spec shim that delegates to the free function.
//
// Why a class wrapper if the implementation is stateless?
//   1. Satisfies the Phase A.3 spec's class-based surface literally.
//   2. Hides the free function name from the public surface in case
//      the canonical name changes (e.g., to `resolve` for parity with
//      other resolver classes in the codebase).
//
// Future stateful extensions (e.g., a cache lease, prewarm hooks, or
// per-frame memoization) will be added in a future commit when the
// concrete need is identified.  At that point the class may either
// grow a stateful constructor or be replaced by a stateful class; the
// current class does NOT speculate about future API shape.
//
// Usage:
//   TextPlacementResolver resolver;
//   auto r = resolver.resolve(canvas, box, placement, anchor, parent);
//
class TextPlacementResolver final {
public:
    // Delegating wrapper around `resolve_text_placement` — see that
    // free function for the full parameter + return contract.
    [[nodiscard]] ResolvedTextPlacement resolve(
        const CanvasInfo& canvas,
        Vec2 box_size,
        TextPlacement placement,
        TextAnchor anchor = TextAnchor::Center,
        const Mat4& layer_matrix = Mat4(1.0f)
    ) const {
        return resolve_text_placement(canvas, box_size, placement,
                                       anchor, layer_matrix);
    }
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
