#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <cmath>

namespace chronon3d::renderer {

namespace {

inline f32 line_weight(f32 distance, f32 thickness) {
    const f32 half = std::max(thickness * 0.5f, 0.0f);
    const f32 feather = 0.85f;
    const f32 edge0 = half + feather;
    const f32 edge1 = half - feather;
    if (distance <= edge1) return 1.0f;
    if (distance >= edge0) return 0.0f;
    return 1.0f - (distance - edge1) / std::max(edge0 - edge1, 1e-6f);
}

inline f32 cell_distance(f32 value, f32 step) {
    if (step <= 1e-6f) {
        return 0.0f;
    }
    const f32 cell = std::round(value / step) * step;
    return std::abs(value - cell);
}

} // namespace

class SoftwareGridBackgroundProcessor final : public ShapeProcessor {
public:
    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state,
              const Camera& camera, i32 width, i32 height) override {
        (void)renderer;
        (void)camera;
        (void)width;
        (void)height;

        const auto& g = node.shape.grid_background;
        if (g.size.x <= 0.0f || g.size.y <= 0.0f) {
            return;
        }

        raster::BBox clip{0, 0, fb.width(), fb.height()};
        if (state.clip_rect) {
            clip = *state.clip_rect;
            clip.clip_to(fb.width(), fb.height());
            if (clip.is_empty()) {
                return;
            }
        }

        Color bg = g.bg_color.to_linear();
        Color minor = g.grid_color.to_linear();
        Color major = minor;
        bg.a *= state.opacity;
        minor.a *= state.opacity;
        major.a = std::min(1.0f, minor.a * 4.0f);

        fb.clear(bg, clip);

        const f32 minor_step = std::max(g.spacing, 1.0f);
        const f32 major_step = minor_step * std::max(1, g.major_every);
        const f32 half_w = static_cast<f32>(fb.width()) * 0.5f;
        const f32 half_h = static_cast<f32>(fb.height()) * 0.5f;
        const f32 origin_x = g.centered ? half_w : 0.0f;
        const f32 origin_y = g.centered ? half_h : 0.0f;
        const f32 offset_x = g.offset.x;
        const f32 offset_y = g.offset.y;
        const f32 minor_thickness = std::max(g.minor_thickness, 0.0f);
        const f32 major_thickness = std::max(g.major_thickness, 0.0f);

        const Color* mask_pixels = nullptr;
        i32 mask_w = 0;
        i32 mask_h = 0;
        if (state.mask && state.mask->enabled()) {
            ensure_mask_alpha_cache(state, fb.width(), fb.height());
            if (state.mask_alpha_cache) {
                mask_pixels = state.mask_alpha_cache->data();
                mask_w = state.mask_alpha_cache->width();
                mask_h = state.mask_alpha_cache->height();
            }
        }

        tbb::parallel_for(tbb::blocked_range<i32>(clip.y0, clip.y1), [&](const tbb::blocked_range<i32>& range) {
            for (i32 y = range.begin(); y != range.end(); ++y) {
                Color* row = fb.pixels_row(y);
                const f32 gy = static_cast<f32>(y) - origin_y - offset_y;
                const f32 minor_dy = cell_distance(gy, minor_step);
                const f32 major_dy = cell_distance(gy, major_step);
                const f32 row_minor = line_weight(minor_dy, minor_thickness);
                const f32 row_major = line_weight(major_dy, major_thickness);

                for (i32 x = clip.x0; x < clip.x1; ++x) {
                    if (mask_pixels) {
                        if (x < 0 || x >= mask_w || y < 0 || y >= mask_h) {
                            continue;
                        }
                        if (mask_pixels[static_cast<usize>(y) * mask_w + x].a <= 0.5f) {
                            continue;
                        }
                    }

                    const f32 gx = static_cast<f32>(x) - origin_x - offset_x;
                    const f32 minor_dx = cell_distance(gx, minor_step);
                    const f32 major_dx = cell_distance(gx, major_step);
                    const f32 col_minor = line_weight(minor_dx, minor_thickness);
                    const f32 col_major = line_weight(major_dx, major_thickness);

                    const f32 minor_alpha = std::max(row_minor, col_minor) * minor.a;
                    const f32 major_alpha = std::max(row_major, col_major) * major.a;

                    if (major_alpha <= 0.0f && minor_alpha <= 0.0f) {
                        continue;
                    }

                    const f32 alpha = std::max(minor_alpha, major_alpha);
                    Color line = (major_alpha >= minor_alpha) ? major : minor;
                    line.a = alpha;
                    row[x] = raster::blend_normal(line, row[x]);
                }
            }
        });
    }

    raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) override {
        (void)model;
        const auto& g = shape.grid_background;
        const f32 pad = std::max(0.0f, spread) + kBBoxSafetyPadding;
        return {
            static_cast<i32>(std::floor(-pad)),
            static_cast<i32>(std::floor(-pad)),
            static_cast<i32>(std::ceil(g.size.x + pad)),
            static_cast<i32>(std::ceil(g.size.y + pad))
        };
    }

    bool hit_test(const Shape& shape, Vec2 local_point, f32 spread) override {
        (void)shape;
        (void)local_point;
        (void)spread;
        return false;
    }
};

std::unique_ptr<ShapeProcessor> create_grid_background_processor() {
    return std::make_unique<SoftwareGridBackgroundProcessor>();
}

} // namespace chronon3d::renderer
