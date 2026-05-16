#include "grid_plane_renderer.hpp"
#include "../rasterizers/line_rasterizer.hpp"
#include <chronon3d/math/mat4.hpp>
#include <chronon3d/math/projector_2_5d.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d {
namespace renderer {

void draw_grid_plane(Framebuffer& fb, const RenderNode& node, const RenderState& state,
                     const GridPlaneShape& s, const GridPlaneRenderState& rt) {
    if (!rt.projection.ready) return;

    const Color col = s.color.to_linear();
    const f32 base_a = col.a * state.opacity;
    if (base_a <= 0.0f) return;

    const Vec3& cp = s.world_pos;
    const int n = static_cast<int>(s.extent / s.spacing) + 1;
    const bool do_fade = s.fade_distance > 0.1f;

    auto segment_alpha = [&](const Vec3& w0, const Vec3& w1) -> f32 {
        if (!do_fade) return base_a;
        const Vec3 mid{(w0.x + w1.x) * 0.5f, (w0.y + w1.y) * 0.5f, (w0.z + w1.z) * 0.5f};
        Vec4 cam = rt.projection.view * Vec4(mid, 1.0f);
        const f32 depth = -cam.z;
        if (depth <= 0.0f) return 0.0f;
        const f32 t = std::clamp(depth / s.fade_distance, 0.0f, 1.0f);
        return base_a * (1.0f - t * (1.0f - s.fade_min_alpha));
    };

    auto draw_segment = [&](const Vec3& w0, const Vec3& w1) {
        const f32 a = segment_alpha(w0, w1);
        if (a <= 0.005f) return;
        Color c = col; c.a = a;
        Vec2 p0, p1;
        if (rt.projection.project_line_clipped(w0, w1, p0, p1))
            bline(fb, p0, p1, c);
    };

    if (s.axis == PlaneAxis::XZ) {
        for (int i = -n; i <= n; ++i) {
            const f32 z = cp.z + i * s.spacing;
            draw_segment({cp.x - s.extent, cp.y, z}, {cp.x + s.extent, cp.y, z});
        }
        for (int i = -n; i <= n; ++i) {
            const f32 x = cp.x + i * s.spacing;
            draw_segment({x, cp.y, cp.z - s.extent}, {x, cp.y, cp.z + s.extent});
        }
    } else {
        for (int i = -n; i <= n; ++i) {
            const f32 y = cp.y + i * s.spacing;
            draw_segment({cp.x - s.extent, y, cp.z}, {cp.x + s.extent, y, cp.z});
        }
        for (int i = -n; i <= n; ++i) {
            const f32 x = cp.x + i * s.spacing;
            draw_segment({x, cp.y - s.extent, cp.z}, {x, cp.y + s.extent, cp.z});
        }
    }
}

} // namespace renderer
} // namespace chronon3d
