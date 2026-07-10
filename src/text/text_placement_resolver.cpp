// SPDX-License-Identifier: MIT
//
// text_placement_resolver.cpp — Unified text placement resolver
//
// ADR-019 Decision 3: TextPlacement resolves the Box coordinate level.
// See text_placement_resolver.hpp for the contract.

#include <chronon3d/text/text_placement_resolver.hpp>
#include <chronon3d/scene/model/render/render_node_factory.hpp> // resolve_text_anchor
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// resolve_placement_origin — compute top-left of the box for a placement
// ═══════════════════════════════════════════════════════════════════════════
//
// For each TextPlacement variant, computes the Vec2 origin (top-left corner
// of the text box) in Canvas coords.  The offset is applied AFTER placement.
//
// Coordinate semantics (ADR-019 Decision 1):
//   - Canvas origin = top-left corner of the frame (0, 0)
//   - Canvas extent = (width, height)
//   - Safe area = inset from each edge by the corresponding margin

Vec2 resolve_placement_origin(
    const CanvasInfo& canvas,
    Vec2 box_size,
    TextPlacement placement,
    Vec2 offset
) {
    Vec2 origin{0.0f, 0.0f};

    switch (placement) {
        case TextPlacement::CanvasCenter:
            origin.x = (canvas.width - box_size.x) * 0.5f;
            origin.y = (canvas.height - box_size.y) * 0.5f;
            break;

        case TextPlacement::TopLeft:
            origin.x = canvas.safe_margin_left;
            origin.y = canvas.safe_margin_top;
            break;

        case TextPlacement::TopCenter:
            origin.x = (canvas.width - box_size.x) * 0.5f;
            origin.y = canvas.safe_margin_top;
            break;

        case TextPlacement::TopRight:
            origin.x = canvas.width - box_size.x - canvas.safe_margin_right;
            origin.y = canvas.safe_margin_top;
            break;

        case TextPlacement::CenterLeft:
            origin.x = canvas.safe_margin_left;
            origin.y = (canvas.height - box_size.y) * 0.5f;
            break;

        case TextPlacement::CenterRight:
            origin.x = canvas.width - box_size.x - canvas.safe_margin_right;
            origin.y = (canvas.height - box_size.y) * 0.5f;
            break;

        case TextPlacement::BottomLeft:
            origin.x = canvas.safe_margin_left;
            origin.y = canvas.height - box_size.y - canvas.safe_margin_bottom;
            break;

        case TextPlacement::BottomCenter:
            origin.x = (canvas.width - box_size.x) * 0.5f;
            origin.y = canvas.height - box_size.y - canvas.safe_margin_bottom;
            break;

        case TextPlacement::BottomRight:
            origin.x = canvas.width - box_size.x - canvas.safe_margin_right;
            origin.y = canvas.height - box_size.y - canvas.safe_margin_bottom;
            break;

        case TextPlacement::SafeAreaTop:
            origin.x = (canvas.width - box_size.x) * 0.5f;
            origin.y = canvas.safe_margin_top;
            break;

        case TextPlacement::SafeAreaBottom:
            origin.x = (canvas.width - box_size.x) * 0.5f;
            origin.y = canvas.height - box_size.y - canvas.safe_margin_bottom;
            break;

        case TextPlacement::Absolute:
            // For Absolute, the origin IS the offset (interpreted as
            // absolute canvas coordinates).
            origin = offset;
            // Don't apply offset again below.
            return origin;
    }

    // Apply user-specified offset (additive).
    origin += offset;
    return origin;
}

// ═══════════════════════════════════════════════════════════════════════════
// resolve_text_placement — the unified resolver
// ═══════════════════════════════════════════════════════════════════════════
//
// Steps:
//   1. Resolve placement → box top-left origin in Canvas coords
//   2. Compute anchor offset within box (TextAnchor → Vec2)
//   3. Compute alignment offset within box (TextAlign + VerticalAlign → Vec2)
//   4. Compose world_matrix = layer_matrix × T(origin + anchor_offset)
//   5. Compute layout_origin = origin + alignment_offset
//
// The layout_origin is where the first glyph's top-left sits in Canvas
// coords.  Glyph positions from the layout engine are relative to this
// origin (ADR-019 Decision 4).

ResolvedTextPlacement resolve_text_placement(
    const CanvasInfo& canvas,
    Vec2 box_size,
    TextPlacement placement,
    Vec2 offset,
    TextAnchor anchor,
    const Mat4& layer_matrix
) {
    ResolvedTextPlacement result;

    // Step 1: Compute the DEFAULT box origin (top-left corner in Canvas
    // coords) assuming TextAnchor::Center.  resolve_placement_origin()
    // centers the box on the placement concept (e.g. CanvasCenter puts
    // the box center at canvas center).
    const Vec2 default_origin = resolve_placement_origin(canvas, box_size, placement, offset);

    // Step 2: Compute anchor adjustment.
    // resolve_placement_origin() assumes TextAnchor::Center — i.e., the
    // box is centered on the placement point.  For other anchors, we
    // shift the origin so the ANCHOR POINT (not the center) aligns
    // with the placement point.
    //
    // adjustment = center_anchor - actual_anchor
    //
    // For TextAnchor::Center: adjust = (0, 0) — no change.
    // For TextAnchor::TopLeft: adjust = (box/2) — box shifts right+down
    //   so the top-left (not center) aligns with the placement point.
    //
    // Example: CanvasCenter + TextAnchor::TopLeft:
    //   default_origin = (110, 360)  (box centered on canvas)
    //   adjust = (850, 180) - (0, 0) = (850, 180)
    //   origin = (960, 540) — box top-left at canvas center
    const Vec3 anchor_vec = resolve_text_anchor(anchor, box_size);
    const Vec3 center_vec = resolve_text_anchor(TextAnchor::Center, box_size);
    const Vec2 adjust = Vec2(center_vec.x - anchor_vec.x, center_vec.y - anchor_vec.y);
    const Vec2 origin = default_origin + adjust;

    // Step 3: Compose world_matrix.
    // The world matrix transforms from Box coords (0,0 → box_size)
    // to Canvas coords at the resolved origin.
    const Vec3 translation(origin.x, origin.y, 0.0f);
    result.world_matrix = layer_matrix * glm::translate(Mat4(1.0f), translation);

    // Step 4: Layout origin = box top-left in Canvas coords.
    // The layout engine positions glyphs relative to this origin.
    // Alignment (TextAlign + VerticalAlign) is a layout-engine concern
    // that positions glyphs WITHIN the box — not handled here.
    result.layout_origin = origin;

    // Populate the remaining fields.
    result.local_frame = Vec4(origin.x, origin.y, box_size.x, box_size.y);
    result.layer_matrix = layer_matrix;

    return result;
}

} // namespace chronon3d
