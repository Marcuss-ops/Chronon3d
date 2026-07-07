#pragma once

// ── text_run_debug_overlay.hpp ─────────────────────────────────────────────
//
// §5 + §6 — Text layout debug overlay + structured log.
//
// Draws 6 colored markers on the framebuffer after text rendering:
//   Red     = canvas center crosshair
//   Blue    = layer/world origin dot
//   Green   = layout box rectangle
//   Yellow  = visual bounds rectangle
//   Violet  = raster surface bounds rectangle
//   White   = alpha centroid crosshair
//
// Also emits a compact [text-layout] structured log per TextRun.
//
// Gated on ctx.policy.text_layout_debug (enabled by --debug-text-layout).
//
// Internal header — lives in src/render_graph/nodes/text_run/, NOT in
// include/chronon3d/.  Zero public API surface.
// ─────────────────────────────────────────────────────────────────────────

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/text/text_run_shape.hpp>

#include <spdlog/spdlog.h>

#include <cmath>
#include <string>

namespace chronon3d::graph::text_run {

// ── Overlay colors ─────────────────────────────────────────────────────────

constexpr Color kOverlayCanvasCenter{1.0f, 0.2f, 0.2f, 0.9f};   // red
constexpr Color kOverlayLayerOrigin{0.2f, 0.4f, 1.0f, 0.9f};     // blue
constexpr Color kOverlayLayoutBox{0.2f, 1.0f, 0.4f, 0.7f};       // green
constexpr Color kOverlayVisualBounds{1.0f, 1.0f, 0.2f, 0.7f};    // yellow
constexpr Color kOverlayRasterSurface{0.8f, 0.2f, 1.0f, 0.7f};   // violet
constexpr Color kOverlayAlphaCentroid{1.0f, 1.0f, 1.0f, 0.9f};   // white

// ── Drawing helpers ────────────────────────────────────────────────────────

/// Draw a crosshair at (cx, cy) with the given arm length and color.
inline void draw_crosshair(Framebuffer& fb, int cx, int cy, int arm, Color c) {
    const int w = static_cast<int>(fb.width());
    const int h = static_cast<int>(fb.height());
    auto put = [&](int x, int y) {
        if (x >= 0 && x < w && y >= 0 && y < h) {
            fb.pixels_row(y)[x] = c;
        }
    };
    for (int i = -arm; i <= arm; ++i) {
        put(cx + i, cy);
        put(cx, cy + i);
    }
}

/// Draw a rectangle outline with the given color.
inline void draw_rect_outline(Framebuffer& fb, int x0, int y0, int x1, int y1, Color c) {
    const int w = static_cast<int>(fb.width());
    const int h = static_cast<int>(fb.height());
    auto put = [&](int x, int y) {
        if (x >= 0 && x < w && y >= 0 && y < h) {
            fb.pixels_row(y)[x] = c;
        }
    };
    for (int x = x0; x <= x1; ++x) { put(x, y0); put(x, y1); }
    for (int y = y0; y <= y1; ++y) { put(x0, y); put(x1, y); }
}

/// Draw a small filled dot at (cx, cy).
inline void draw_dot(Framebuffer& fb, int cx, int cy, Color c) {
    const int w = static_cast<int>(fb.width());
    const int h = static_cast<int>(fb.height());
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            int x = cx + dx;
            int y = cy + dy;
            if (x >= 0 && x < w && y >= 0 && y < h) {
                fb.pixels_row(y)[x] = c;
            }
        }
    }
}

// ── Alpha centroid computation ─────────────────────────────────────────────

struct AlphaCentroid {
    f32 x{-1.0f};
    f32 y{-1.0f};
    f32 max_alpha{0.0f};
    int area{0};
    int bbox_x0{0}, bbox_y0{0}, bbox_x1{0}, bbox_y1{0};
    bool touches_edge{false};
};

[[nodiscard]] inline AlphaCentroid compute_overlay_centroid(
    const Framebuffer& fb, float alpha_threshold = 0.05f
) {
    AlphaCentroid c{};
    double sum_x = 0.0, sum_y = 0.0, sum_w = 0.0;
    const int w = static_cast<int>(fb.width());
    const int h = static_cast<int>(fb.height());
    c.bbox_x0 = w; c.bbox_y0 = h; c.bbox_x1 = 0; c.bbox_y1 = 0;

    for (int y = 0; y < h; ++y) {
        const Color* row = fb.pixels_row(y);
        for (int x = 0; x < w; ++x) {
            const float a = row[x].a;
            if (a > c.max_alpha) c.max_alpha = a;
            if (a > alpha_threshold) {
                sum_x += static_cast<double>(x) * a;
                sum_y += static_cast<double>(y) * a;
                sum_w += a;
                ++c.area;
                if (x < c.bbox_x0) c.bbox_x0 = x;
                if (x > c.bbox_x1) c.bbox_x1 = x;
                if (y < c.bbox_y0) c.bbox_y0 = y;
                if (y > c.bbox_y1) c.bbox_y1 = y;
                if (x == 0 || x == w - 1 || y == 0 || y == h - 1)
                    c.touches_edge = true;
            }
        }
    }
    if (sum_w > 0.0) {
        c.x = static_cast<f32>(sum_x / sum_w);
        c.y = static_cast<f32>(sum_y / sum_w);
    }
    return c;
}

// ── Main overlay + log ─────────────────────────────────────────────────────

/// Draw debug overlay markers on the framebuffer and emit structured log.
/// Call from TextRunNode::execute() when ctx.policy.text_layout_debug is true.
inline void draw_text_debug_overlay(
    Framebuffer& fb,
    const TextRunShape& shape,
    const std::string& node_name,
    const Mat4& world_matrix,
    f32 opacity,
    std::size_t items_drawn,
    bool use_local,
    const Vec3& node_pos
) {
    const int w = static_cast<int>(fb.width());
    const int h = static_cast<int>(fb.height());
    const int arm = std::max(20, std::min(w, h) / 40);

    // 1. Red crosshair at canvas center
    draw_crosshair(fb, w / 2, h / 2, arm, kOverlayCanvasCenter);

    // 2. Blue dot at world origin (matrix translation column)
    const int ox = static_cast<int>(world_matrix[3][0]);
    const int oy = static_cast<int>(world_matrix[3][1]);
    draw_dot(fb, ox, oy, kOverlayLayerOrigin);

    // 3. Green rectangle at layout box (centered at world origin)
    if (shape.layout_spec.box.x > 0 && shape.layout_spec.box.y > 0) {
        const int bw = static_cast<int>(shape.layout_spec.box.x);
        const int bh = static_cast<int>(shape.layout_spec.box.y);
        const int bx0 = ox - bw / 2;
        const int by0 = oy - bh / 2;
        draw_rect_outline(fb, bx0, by0, bx0 + bw, by0 + bh, kOverlayLayoutBox);
    }

    // 4. Compute alpha centroid
    auto centroid = compute_overlay_centroid(fb);

    // 5. Yellow rectangle at alpha bbox (visual bounds)
    if (centroid.area > 0) {
        draw_rect_outline(fb, centroid.bbox_x0, centroid.bbox_y0,
                          centroid.bbox_x1, centroid.bbox_y1, kOverlayVisualBounds);
    }

    // 6. White crosshair at alpha centroid
    if (centroid.x >= 0 && centroid.y >= 0) {
        draw_crosshair(fb, static_cast<int>(centroid.x),
                       static_cast<int>(centroid.y), arm / 2, kOverlayAlphaCentroid);
    }

    // 7. Structured log (§6)
    const f32 cx = static_cast<f32>(w) * 0.5f;
    const f32 cy = static_cast<f32>(h) * 0.5f;
    const f32 delta_x = centroid.x >= 0 ? centroid.x - cx : -1.0f;
    const f32 delta_y = centroid.y >= 0 ? centroid.y - cy : -1.0f;

    spdlog::info(
        "[text-layout] name={} use_local={} "
        "layer_pos=({:.1f},{:.1f},{:.1f}) "
        "node_pos=({:.1f},{:.1f},{:.1f}) "
        "matrix_final_t=({:.1f},{:.1f},{:.1f}) "
        "layout_box=({:.0f},{:.0f}) "
        "visual_bounds=({},{},{},{}) "
        "raster_surface={}x{} "
        "alpha_bbox=({},{},{},{}) "
        "alpha_centroid=({:.1f},{:.1f}) "
        "delta_center=({:+.1f},{:+.1f}) "
        "max_alpha={:.3f} area={} "
        "clipped={} opacity={:.2f} items_drawn={}",
        node_name,
        use_local ? 1 : 0,
        world_matrix[3][0], world_matrix[3][1], world_matrix[3][2],
        node_pos.x, node_pos.y, node_pos.z,
        world_matrix[3][0], world_matrix[3][1], world_matrix[3][2],
        shape.layout_spec.box.x, shape.layout_spec.box.y,
        centroid.bbox_x0, centroid.bbox_y0, centroid.bbox_x1, centroid.bbox_y1,
        w, h,
        centroid.bbox_x0, centroid.bbox_y0, centroid.bbox_x1, centroid.bbox_y1,
        centroid.x, centroid.y,
        delta_x, delta_y,
        centroid.max_alpha, centroid.area,
        centroid.touches_edge ? "yes" : "no",
        opacity, items_drawn
    );
}

} // namespace chronon3d::graph::text_run
