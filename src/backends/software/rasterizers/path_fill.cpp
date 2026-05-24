#include "path_fill.hpp"
#include "path_utils.hpp"
#include <algorithm>
#include <glm/glm.hpp>

namespace chronon3d::renderer {

Color resolve_fill_color(const Fill& fill, Vec2 p, const raster::BBox& bbox, f32 opacity) {
    if (fill.type == FillType::Solid) {
        Color c = fill.solid.to_linear();
        c.a *= opacity;
        return c;
    }

    const f32 w = std::max(1.0f, static_cast<f32>(bbox.x1 - bbox.x0));
    const f32 h = std::max(1.0f, static_cast<f32>(bbox.y1 - bbox.y0));
    const Vec2 local{
        (p.x - static_cast<f32>(bbox.x0)) / w,
        (p.y - static_cast<f32>(bbox.y0)) / h
    };

    f32 t = 0.0f;
    if (fill.type == FillType::LinearGradient) {
        const Vec2 dir = fill.gradient.to - fill.gradient.from;
        const f32 len2 = glm::dot(dir, dir);
        if (len2 > kPathEpsilon) {
            const Vec2 d = local - fill.gradient.from;
            t = glm::dot(d, dir) / len2;
        }
    } else {
        const Vec2 d = local - fill.gradient.from;
        const Vec2 rv = fill.gradient.to - fill.gradient.from;
        const f32 r = glm::length(rv);
        t = (r > kPathEpsilon) ? glm::length(d) / r : 0.0f;
    }

    t = std::clamp(t, 0.0f, 1.0f);
    Color c = sample_gradient(fill.gradient, t).to_linear();
    c.a *= opacity;
    return c;
}

} // namespace chronon3d::renderer
