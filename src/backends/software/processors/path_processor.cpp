#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/software/rasterizers/path_rasterizer.hpp>
#include <chronon3d/math/path_utils.hpp>
#include <chronon3d/math/math_base.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d::renderer {

class PathProcessor : public ShapeProcessor {
public:
    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node,
              const RenderState& state, const Camera& camera, i32 width, i32 height) override {
        PathRasterizer::draw_path(fb, node, state, camera, width, height);
    }

    raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) override {
        auto subpaths = math::flatten_path(shape.path);
        f32 min_x = 1e10f, max_x = -1e10f;
        f32 min_y = 1e10f, max_y = -1e10f;
        f32 padding = shape.path.stroke.width * 0.5f + spread;

        bool empty = true;
        for (const auto& sub : subpaths) {
            for (const auto& p : sub) {
                Vec2 lp = p;
                Vec2 corners[4] = {
                    lp + Vec2(-padding, -padding),
                    lp + Vec2(padding, -padding),
                    lp + Vec2(padding, padding),
                    lp + Vec2(-padding, padding)
                };
                for (int i = 0; i < 4; ++i) {
                    Vec4 p_world = model * Vec4(corners[i].x, corners[i].y, 0.0f, 1.0f);
                    f32 w = std::abs(p_world.w) > 1e-7f ? p_world.w : 1.0f;
                    f32 px = p_world.x / w;
                    f32 py = p_world.y / w;
                    min_x = std::min(min_x, px);
                    max_x = std::max(max_x, px);
                    min_y = std::min(min_y, py);
                    max_y = std::max(max_y, py);
                }
                empty = false;
            }
        }

        if (empty) return {0, 0, 0, 0};

        return {
            static_cast<i32>(std::floor(min_x)),
            static_cast<i32>(std::floor(min_y)),
            static_cast<i32>(std::ceil(max_x)) + 1,
            static_cast<i32>(std::ceil(max_y)) + 1
        };
    }

    bool hit_test(const Shape& shape, Vec2 local_point, f32 spread) override {
        auto subpaths = math::flatten_path(shape.path);
        f32 radius_sq = std::pow(shape.path.stroke.width * 0.5f + spread, 2.0f);

        for (const auto& sub : subpaths) {
            for (size_t i = 1; i < sub.size(); ++i) {
                Vec2 ab = sub[i] - sub[i-1];
                f32 ab_len_sq = glm::dot(ab, ab);
                if (ab_len_sq < 1e-6f) {
                    Vec2 diff = local_point - sub[i-1];
                    if (glm::dot(diff, diff) <= radius_sq) return true;
                    continue;
                }
                f32 t = std::clamp(glm::dot(local_point - sub[i-1], ab) / ab_len_sq, 0.0f, 1.0f);
                Vec2 closest = sub[i-1] + ab * t;
                Vec2 diff = local_point - closest;
                if (glm::dot(diff, diff) <= radius_sq) return true;
            }
        }
        return false;
    }
};

std::unique_ptr<ShapeProcessor> create_path_processor() {
    return std::make_unique<PathProcessor>();
}

} // namespace chronon3d::renderer
