#include <chronon3d/backends/software/rasterizers/card3d_material_rasterizer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include "scanline_rasterizer.hpp"
#include <algorithm>
#include <cmath>

namespace chronon3d::renderer {

// ─── Helpers ──────────────────────────────────────────────────────────────────

// Compute a normalized vector from two points.
static Vec2 normalize_v2(Vec2 v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y);
    if (len < 1e-8f) return {0.0f, 0.0f};
    return {v.x / len, v.y / len};
}

// Extrude a screen-space corner along a direction by thickness_px.
static Vec2 extrude(Vec2 corner, Vec2 dir, float thickness) {
    return {corner.x + dir.x * thickness, corner.y + dir.y * thickness};
}

// Compute the screen-space extrusion direction from a world-space light direction.
// Returns normalized 2D vector for offsetting side faces.
static Vec2 light_extrude_dir(const Vec3& light_dir) {
    // Project light direction onto screen XY plane (ignore Z for screen offset).
    Vec2 dir{light_dir.x, -light_dir.y}; // flip Y because screen Y is inverted
    return normalize_v2(dir);
}

// ─── Public API ───────────────────────────────────────────────────────────────

void render_card3d(
    Framebuffer& fb,
    const rendering::ProjectedCard& card,
    const Card3DMaterial& material,
    float opacity
) {
    if (!card.visible || opacity <= 0.0f) return;

    // Card corners in screen space: TL=0, TR=1, BR=2, BL=3
    const Vec2& TL = card.corners[0];
    const Vec2& TR = card.corners[1];
    const Vec2& BR = card.corners[2];
    const Vec2& BL = card.corners[3];

    // Extrusion direction from light
    const Vec2 extrude_dir = light_extrude_dir(material.light_direction);
    const float t = material.thickness_px;

    // ── Step 1: Side faces ────────────────────────────────────────────────
    // Two side faces are visible depending on light direction:
    //   Right face:   TR→BR edge, extruded along extrude_dir
    //   Bottom face:  BR→BL edge, extruded along extrude_dir
    //
    // The side faces are rendered first (behind the front face).
    // We determine which edges are visible based on the light direction sign.

    auto side_quad = [&](Vec2 e0, Vec2 e1) {
        // Build a 4-point quad from edge e0→e1 extruded by t along extrude_dir
        const Vec2 q[4] = {
            e0,
            e1,
            extrude(e1, extrude_dir, t),
            extrude(e0, extrude_dir, t),
        };
        fill_convex_quad(fb, q, material.side_color.with_alpha(material.side_color.a * opacity));
    };

    // Determine which sides are visible:
    // If extrude_dir points right/down, the right and bottom sides are visible.
    // If extrude_dir points left/up, the left and top sides are visible instead.
    const bool show_right = extrude_dir.x > 0.0f;
    const bool show_bottom = extrude_dir.y > 0.0f;

    if (show_right) {
        // Right side face: TR → BR, extruded right
        side_quad(TR, BR);
    } else {
        // Left side face: TL → BL, extruded left
        side_quad(BL, TL);
    }

    if (show_bottom) {
        // Bottom side face: BR → BL, extruded down
        side_quad(BR, BL);
    } else {
        // Top side face: TL → TR, extruded up
        side_quad(TR, TL);
    }

    // ── Step 2: Front face with gradient ──────────────────────────────────
    // Front face uses top→bottom gradient (always rendered, on top of sides)
    Color gc[4] = {
        material.front_top_color.with_alpha(material.front_top_color.a * opacity),    // TL
        material.front_top_color.with_alpha(material.front_top_color.a * opacity),    // TR
        material.front_bottom_color.with_alpha(material.front_bottom_color.a * opacity), // BR
        material.front_bottom_color.with_alpha(material.front_bottom_color.a * opacity), // BL
    };
    fill_gradient_quad(fb, card.corners, gc);

    // ── Step 3: Edge highlight (thin strip along top and left edges) ───────
    if (material.edge_highlight_intensity > 0.0f && material.edge_highlight_color.a > 0.0f) {
        const float hl_w = std::max(1.5f, t * 0.12f); // highlight width
        const Color hl_c = material.edge_highlight_color.with_alpha(
            material.edge_highlight_color.a * material.edge_highlight_intensity * opacity
        );

        // Edge highlight along the visible top/left edges.
        // Uses thin quads extruded outward from the edge.

        // Top edge highlight: TL→TR, extruded upward
        {
            const Vec2 e0 = TL;
            const Vec2 e1 = TR;
            // outward normal: perpendicular to edge, pointing up
            Vec2 edge_dir = normalize_v2({e1.x - e0.x, e1.y - e0.y});
            Vec2 out_normal = {-edge_dir.y, edge_dir.x}; // rotate 90° CCW (up for top edge)
            // Ensure outward (away from card center)
            Vec2 center = {(TL.x + TR.x + BR.x + BL.x) * 0.25f, (TL.y + TR.y + BR.y + BL.y) * 0.25f};
            Vec2 to_center = {center.x - (e0.x + e1.x) * 0.5f, center.y - (e0.y + e1.y) * 0.5f};
            if (out_normal.x * to_center.x + out_normal.y * to_center.y > 0.0f) {
                out_normal = {-out_normal.x, -out_normal.y};
            }
            const Vec2 hl[4] = {
                e0,
                e1,
                {e1.x + out_normal.x * hl_w, e1.y + out_normal.y * hl_w},
                {e0.x + out_normal.x * hl_w, e0.y + out_normal.y * hl_w},
            };
            fill_convex_quad(fb, hl, hl_c);
        }

        // Left edge highlight: TL→BL, extruded left
        {
            const Vec2 e0 = TL;
            const Vec2 e1 = BL;
            Vec2 edge_dir = normalize_v2({e1.x - e0.x, e1.y - e0.y});
            Vec2 out_normal = {edge_dir.y, -edge_dir.x}; // rotate 90° CW (left for left edge)
            Vec2 center = {(TL.x + TR.x + BR.x + BL.x) * 0.25f, (TL.y + TR.y + BR.y + BL.y) * 0.25f};
            Vec2 to_center = {center.x - (e0.x + e1.x) * 0.5f, center.y - (e0.y + e1.y) * 0.5f};
            if (out_normal.x * to_center.x + out_normal.y * to_center.y > 0.0f) {
                out_normal = {-out_normal.x, -out_normal.y};
            }
            const Vec2 hl[4] = {
                e0,
                e1,
                {e1.x + out_normal.x * hl_w, e1.y + out_normal.y * hl_w},
                {e0.x + out_normal.x * hl_w, e0.y + out_normal.y * hl_w},
            };
            fill_convex_quad(fb, hl, hl_c);
        }
    }

    // ── Step 4: Rim light (additive overlay on front face) ─────────────────
    if (material.rim_light_intensity > 0.0f && material.rim_light_color.a > 0.0f) {
        // Rim light is brighter on the edges facing the light.
        // We approximate this by adding a thin bright border on the top and right edges
        // where the rim would catch the light.
        const float rim_w = std::max(2.0f, t * 0.20f);
        const Color rim_c = material.rim_light_color.with_alpha(
            material.rim_light_color.a * material.rim_light_intensity * opacity * 0.5f
        );

        // Top edge rim light
        {
            const Vec2 e0 = TL;
            const Vec2 e1 = TR;
            Vec2 edge_dir = normalize_v2({e1.x - e0.x, e1.y - e0.y});
            Vec2 out_normal = {-edge_dir.y, edge_dir.x};
            Vec2 center = {(TL.x + TR.x + BR.x + BL.x) * 0.25f, (TL.y + TR.y + BR.y + BL.y) * 0.25f};
            Vec2 to_center = {center.x - (e0.x + e1.x) * 0.5f, center.y - (e0.y + e1.y) * 0.5f};
            if (out_normal.x * to_center.x + out_normal.y * to_center.y > 0.0f) {
                out_normal = {-out_normal.x, -out_normal.y};
            }
            const Vec2 r[4] = {
                e0,
                e1,
                {e1.x + out_normal.x * rim_w, e1.y + out_normal.y * rim_w},
                {e0.x + out_normal.x * rim_w, e0.y + out_normal.y * rim_w},
            };
            fill_convex_quad(fb, r, rim_c);
        }

        // Right edge rim light
        {
            const Vec2 e0 = TR;
            const Vec2 e1 = BR;
            Vec2 edge_dir = normalize_v2({e1.x - e0.x, e1.y - e0.y});
            Vec2 out_normal = {-edge_dir.y, edge_dir.x};
            Vec2 center = {(TL.x + TR.x + BR.x + BL.x) * 0.25f, (TL.y + TR.y + BR.y + BL.y) * 0.25f};
            Vec2 to_center = {center.x - (e0.x + e1.x) * 0.5f, center.y - (e0.y + e1.y) * 0.5f};
            if (out_normal.x * to_center.x + out_normal.y * to_center.y > 0.0f) {
                out_normal = {-out_normal.x, -out_normal.y};
            }
            const Vec2 r[4] = {
                e0,
                e1,
                {e1.x + out_normal.x * rim_w, e1.y + out_normal.y * rim_w},
                {e0.x + out_normal.x * rim_w, e0.y + out_normal.y * rim_w},
            };
            fill_convex_quad(fb, r, rim_c);
        }
    }
}

} // namespace chronon3d::renderer
