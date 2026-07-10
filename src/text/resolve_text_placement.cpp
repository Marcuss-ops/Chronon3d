// SPDX-License-Identifier: MIT
//
// resolve_text_placement.cpp — Unified text placement resolver
//
// Phase A6 close-out (2026-07-10).  Implements the free-function resolver
// declared in `include/chronon3d/text/resolve_text_placement.hpp`.  The
// previous `text_placement_resolver.{hpp,cpp}` pair co-resident with the
// also-retired `class TextPlacementResolver` wrapper has been split: the
// free-function surface moved here, the class wrapper is GONE.
//
// ADR-019 Decision 3: TextPlacement resolves the Box coordinate level.
// See `include/chronon3d/text/resolve_text_placement.hpp` for the contract.

#include <chronon3d/text/resolve_text_placement.hpp>
#include <chronon3d/scene/model/render/render_node_factory.hpp> // resolve_text_anchor
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// resolve_placement_origin — compute the PIN POINT for a placement
// ═══════════════════════════════════════════════════════════════════════════
//
// Returns the "pin point" — the Canvas-space position where the box's
// ANCHOR POINT should sit.  This is NOT the box's top-left corner;
// the box top-left is derived by subtracting the anchor offset:
//
//   box_top_left = pin_point - anchor_offset
//
// For CanvasCenter:  pin_point = (canvas.width/2, canvas.height/2)
// For TopLeft:       pin_point = (safe_margin_left, safe_margin_top)
// For Absolute:      pin_point = offset (the explicit position)
//
// The offset parameter is additive for all placements except Absolute
// (where it IS the position).
//
// Coordinate semantics (ADR-019 Decision 1):
//   - Canvas origin = top-left corner of the frame (0, 0)
//   - Canvas extent = (width, height)
//   - Safe area = inset from each edge by the corresponding margin

Vec2 resolve_placement_origin(
    const CanvasInfo& canvas,
    Vec2 box_size,
    TextPlacement placement
) {
    Vec2 pin{0.0f, 0.0f};
    (void)box_size; // pin point is independent of box size for most placements

    switch (placement.kind) {
        case TextPlacementKind::CanvasCenter:
            pin.x = canvas.width * 0.5f;
            pin.y = canvas.height * 0.5f;
            break;

        case TextPlacementKind::TopLeft:
            pin.x = canvas.safe_margin_left;
            pin.y = canvas.safe_margin_top;
            break;

        case TextPlacementKind::TopCenter:
            pin.x = canvas.width * 0.5f;
            pin.y = canvas.safe_margin_top;
            break;

        case TextPlacementKind::TopRight:
            pin.x = canvas.width - canvas.safe_margin_right;
            pin.y = canvas.safe_margin_top;
            break;

        case TextPlacementKind::CenterLeft:
            pin.x = canvas.safe_margin_left;
            pin.y = canvas.height * 0.5f;
            break;

        case TextPlacementKind::Center:                // Phase A.2: explicit canvas center
            pin.x = canvas.width * 0.5f;
            pin.y = canvas.height * 0.5f;
            break;

        case TextPlacementKind::CenterRight:
            pin.x = canvas.width - canvas.safe_margin_right;
            pin.y = canvas.height * 0.5f;
            break;

        case TextPlacementKind::BottomLeft:
            pin.x = canvas.safe_margin_left;
            pin.y = canvas.height - canvas.safe_margin_bottom;
            break;

        case TextPlacementKind::BottomCenter:
            pin.x = canvas.width * 0.5f;
            pin.y = canvas.height - canvas.safe_margin_bottom;
            break;

        case TextPlacementKind::BottomRight:
            pin.x = canvas.width - canvas.safe_margin_right;
            pin.y = canvas.height - canvas.safe_margin_bottom;
            break;

        case TextPlacementKind::SafeAreaTop:
            pin.x = canvas.width * 0.5f;
            pin.y = canvas.safe_margin_top;
            break;

        case TextPlacementKind::SafeAreaBottom:
            pin.x = canvas.width * 0.5f;
            pin.y = canvas.height - canvas.safe_margin_bottom;
            break;

        case TextPlacementKind::SafeAreaCenter:        // Phase A.2: center of safe area bounds
            pin.x = (canvas.safe_margin_left + (canvas.width - canvas.safe_margin_right)) * 0.5f;
            pin.y = (canvas.safe_margin_top + (canvas.height - canvas.safe_margin_bottom)) * 0.5f;
            break;

        case TextPlacementKind::Absolute:
            // For Absolute, the offset IS the pin point.
            return placement.offset;
    }

    // Apply user-specified offset (additive).
    pin += placement.offset;
    return pin;
}

// ═══════════════════════════════════════════════════════════════════════════
// resolve_text_placement — the canonical unified resolver
// ═══════════════════════════════════════════════════════════════════════════
//
// Steps:
//   1. Resolve pin point (where the anchor should be on the canvas)
//   2. Compute anchor offset (which point of the box is the anchor)
//   3. Compute box top-left: origin = pin_point - anchor_offset
//   4. Compose world_matrix = layer_matrix × T(origin)
//   5. Set layout_origin = origin (top-left of the box)
//
// The pin point is the Canvas-space position where the box's anchor
// point should sit.  The anchor offset tells us which point of the
// box is the "pin" (center, top-left, bottom-center, etc.).
//
// Example: CanvasCenter + TextAnchor::Center on 1920×1080 with 1700×360 box:
//   pin_point = (960, 540)  (canvas center)
//   anchor_offset = (850, 180)  (box_size/2 for Center)
//   box_top_left = (960-850, 540-180) = (110, 360)
//
// Example: CanvasCenter + TextAnchor::TopLeft:
//   pin_point = (960, 540)
//   anchor_offset = (0, 0)  (TopLeft = no offset)
//   box_top_left = (960, 540)  (box starts at canvas center)

ResolvedTextPlacement resolve_text_placement(
    const CanvasInfo& canvas,
    Vec2 box_size,
    TextPlacement placement,
    TextAnchor anchor,
    const Mat4& layer_matrix
) {
    ResolvedTextPlacement result;

    // Step 1: Resolve the pin point — where the box's anchor should be.
    // The placement struct bundles kind + offset (Phase A.2 consolidation).
    const Vec2 pin_point = resolve_placement_origin(canvas, box_size, placement);

    // Step 2: Compute anchor offset within the box.
    // resolve_text_anchor() returns the anchor point RELATIVE to the
    // box's top-left corner.  For TextAnchor::Center, this is
    // (box_size.x/2, box_size.y/2, 0).  For TextAnchor::TopLeft, it's (0,0,0).
    const Vec3 anchor_offset = resolve_text_anchor(anchor, box_size);

    // Step 3: Compute box origin (top-left corner in Canvas coords).
    // box_top_left = pin_point - anchor_offset
    const Vec2 origin = Vec2(
        pin_point.x - anchor_offset.x,
        pin_point.y - anchor_offset.y
    );

    // Step 4: Compose world_matrix.
    // The world matrix transforms from Box coords (0,0 → box_size)
    // to Canvas coords at the resolved origin.
    const Vec3 translation(origin.x, origin.y, 0.0f);
    result.world_matrix = layer_matrix * glm::translate(Mat4(1.0f), translation);

    // Step 5: Layout origin = box top-left in Canvas coords.
    // The layout engine positions glyphs relative to this origin.
    // Alignment (TextAlign + VerticalAlign) is a layout-engine concern
    // that positions glyphs WITHIN the box — not handled here.
    result.layout_origin = origin;

    // Populate the remaining fields.
    result.local_frame = Vec4(origin.x, origin.y, box_size.x, box_size.y);
    result.layer_matrix = layer_matrix;

    // Phase A.3 — echo the input anchor so downstream consumers
    // (renderer, future builder API) can re-derive layout without
    // re-running the full resolver.  Defaults to TextAnchor::Center
    // (matching the free function's default parameter).
    result.resolved_anchor = anchor;

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// F3.B — SafeAreaPreset constants and CanvasInfo factory
// ═══════════════════════════════════════════════════════════════════════════

const SafeAreaPreset SafeAreaPreset::Landscape16x9{0.05f, 0.05f, 0.05f, 0.05f};
const SafeAreaPreset SafeAreaPreset::Portrait9x16{0.05f, 0.05f, 0.05f, 0.05f};
const SafeAreaPreset SafeAreaPreset::Square1x1{0.05f, 0.05f, 0.05f, 0.05f};
const SafeAreaPreset SafeAreaPreset::Landscape4x3{0.05f, 0.05f, 0.05f, 0.05f};

CanvasInfo CanvasInfo::with_safe_area(
    f32 width, f32 height, const SafeAreaPreset& preset)
{
    CanvasInfo info;
    info.width              = width;
    info.height             = height;
    info.safe_margin_top    = height * preset.margin_top_fraction;
    info.safe_margin_bottom = height * preset.margin_bottom_fraction;
    info.safe_margin_left   = width * preset.margin_left_fraction;
    info.safe_margin_right  = width * preset.margin_right_fraction;
    return info;
}

} // namespace chronon3d
