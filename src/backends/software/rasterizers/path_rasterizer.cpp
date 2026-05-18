#include <chronon3d/backends/software/rasterizers/path_rasterizer.hpp>
#include <chronon3d/math/path_utils.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/backends/software/software_compositor.hpp>
#include <algorithm>

namespace chronon3d::renderer {

namespace {

bool point_near_segment(Vec2 p, Vec2 a, Vec2 b, f32 radius_sq, f32& out_t) {
    Vec2 ab = b - a;
    f32 ab_len_sq = glm::dot(ab, ab);
    if (ab_len_sq < 1e-6f) {
        out_t = 0.0f;
        Vec2 diff = p - a;
        return glm::dot(diff, diff) <= radius_sq;
    }
    f32 t = glm::dot(p - a, ab) / ab_len_sq;
    out_t = std::clamp(t, 0.0f, 1.0f);
    Vec2 closest = a + ab * out_t;
    Vec2 diff = p - closest;
    return glm::dot(diff, diff) <= radius_sq;
}

} // namespace

void PathRasterizer::draw_path(Framebuffer& fb, const RenderNode& node, const RenderState& state,
                              const Camera& camera, i32 width, i32 height) {
    const auto& path = node.shape.path;
    if (path.commands.empty()) return;

    // 1. Flattening
    auto subpaths = math::flatten_path(path);

    // 2. Trimming & Length calculation
    std::vector<std::vector<Vec2>> visible_paths;
    for (const auto& sub : subpaths) {
        auto trimmed = math::trim_polyline(sub, path.stroke.trim_start, path.stroke.trim_end);
        if (!trimmed.empty()) {
            visible_paths.push_back(std::move(trimmed));
        }
    }

    if (visible_paths.empty()) return;

    // 3. Stroke Rasterization (MVP)
    const Mat4 world_matrix = node.world_transform.to_matrix();
    const Mat4 inv_world = glm::inverse(world_matrix);
    
    Vec2 min_p{1e10f, 1e10f}, max_p{-1e10f, -1e10f};
    f32 padding = path.stroke.width * 0.5f;
    for (const auto& sub : visible_paths) {
        for (const auto& p : sub) {
            min_p = glm::min(min_p, p);
            max_p = glm::max(max_p, p);
        }
    }
    min_p -= Vec2(padding);
    max_p += Vec2(padding);

    Vec2 corners[4] = { min_p, {max_p.x, min_p.y}, max_p, {min_p.x, max_p.y} };

    i32 s_min_x = width, s_max_x = 0, s_min_y = height, s_max_y = 0;
    for (int i = 0; i < 4; ++i) {
        Vec3 p_world = Vec3(world_matrix * Vec4(corners[i].x, corners[i].y, 0.0f, 1.0f));
        s_min_x = std::min(s_min_x, static_cast<i32>(std::floor(p_world.x)));
        s_max_x = std::max(s_max_x, static_cast<i32>(std::ceil(p_world.x)));
        s_min_y = std::min(s_min_y, static_cast<i32>(std::floor(p_world.y)));
        s_max_y = std::max(s_max_y, static_cast<i32>(std::ceil(p_world.y)));
    }

    s_min_x = std::clamp(s_min_x, 0, width - 1);
    s_max_x = std::clamp(s_max_x, 0, width - 1);
    s_min_y = std::clamp(s_min_y, 0, height - 1);
    s_max_y = std::clamp(s_max_y, 0, height - 1);

    f32 radius_sq = padding * padding;
    Color stroke_color = node.color; 

    for (i32 y = s_min_y; y <= s_max_y; ++y) {
        for (i32 x = s_min_x; x <= s_max_x; ++x) {
            Vec2 screen_p{static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f};
            Vec4 local_p4 = inv_world * Vec4(screen_p.x, screen_p.y, 0.0f, 1.0f);
            Vec2 lp{local_p4.x, local_p4.y};

            bool hit = false;
            for (const auto& sub : visible_paths) {
                for (size_t i = 1; i < sub.size(); ++i) {
                    f32 t;
                    if (point_near_segment(lp, sub[i-1], sub[i], radius_sq, t)) {
                        hit = true;
                        break;
                    }
                }
                if (hit) break;
            }

            if (hit) {
                Color dst = fb.get_pixel(x, y);
                fb.set_pixel(x, y, compositor::blend(stroke_color, dst, BlendMode::Normal));
            }
        }
    }
}

} // namespace chronon3d::renderer
