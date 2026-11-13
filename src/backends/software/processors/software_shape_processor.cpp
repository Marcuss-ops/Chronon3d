#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include "../rasterizers/shape_rasterizer.hpp"
#include "../effects/render_effects_processor.hpp"
// R2: draw() now consumes the slim processor context POD.
#include <chronon3d/backends/software/software_processor_context.hpp>
#include <chronon3d/compositor/alpha.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/runtime/gpu_layer_batch.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d::renderer {

namespace {
void draw_environment_particle(Framebuffer& fb, const RenderNode& node,
                               const RenderState& state) {
    const auto& particle = *node.software_environment_particle;
    const Mat4 emitter = state.matrix * glm::inverse(node.world_transform.to_mat4());
    const Vec4 head = emitter * Vec4{particle.position.x, particle.position.y, particle.z, 1.0f};
    const float sx = glm::length(Vec2{emitter[0][0], emitter[1][0]});
    const float sy = glm::length(Vec2{emitter[0][1], emitter[1][1]});
    const float radius = particle.size * 0.5f * std::min(sx, sy);
    if (!std::isfinite(head.x) || !std::isfinite(head.y) ||
        !std::isfinite(radius) || radius <= 0.0f) return;
    Vec2 motion{
        emitter[0][0] * particle.velocity.x + emitter[1][0] * particle.velocity.y,
        emitter[0][1] * particle.velocity.x + emitter[1][1] * particle.velocity.y};
    motion *= runtime::kParticleTrailSeconds;
    const float limit = std::max(particle.size * runtime::kParticleTrailMaxSizeFactor,
                                 runtime::kParticleTrailMinPixels);
    const float length = glm::length(motion);
    if (std::isfinite(length) && length > limit && length > 0.0f)
        motion *= limit / length;
    if (!std::isfinite(motion.x) || !std::isfinite(motion.y)) motion = Vec2{0.0f};
    const float alpha = std::clamp(particle.color.a * state.opacity, 0.0f, 1.0f);
    const bool bokeh = node.fill.type != FillType::Solid &&
        node.fill.gradient.type == graphics::GradientType::Radial;
    raster::BBox box{
        static_cast<i32>(std::floor(std::min(head.x, head.x + motion.x) - radius)),
        static_cast<i32>(std::floor(std::min(head.y, head.y + motion.y) - radius)),
        static_cast<i32>(std::ceil(std::max(head.x, head.x + motion.x) + radius)),
        static_cast<i32>(std::ceil(std::max(head.y, head.y + motion.y) + radius))};
    box.clip_to(fb.width(), fb.height());
    if (state.clip_rect) {
        box.x0 = std::max(box.x0, state.clip_rect->x0);
        box.y0 = std::max(box.y0, state.clip_rect->y0);
        box.x1 = std::min(box.x1, state.clip_rect->x1);
        box.y1 = std::min(box.y1, state.clip_rect->y1);
    }
    if (state.mask && state.mask->enabled())
        ensure_mask_alpha_cache(state, fb.width(), fb.height());
    const float motion_length2 = glm::dot(motion, motion);
    for (i32 y = box.y0; y < box.y1; ++y) {
        Color* row = fb.pixels_row(y);
        for (i32 x = box.x0; x < box.x1; ++x) {
            if (state.mask && !pixel_passes_mask(state, x, y)) continue;
            const Vec2 p{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
            const Vec2 origin{head.x, head.y};
            const float along = motion_length2 > 1e-6f
                ? std::clamp(glm::dot(p - origin, motion) / motion_length2, 0.0f, 1.0f)
                : 0.0f;
            const float normalized = glm::length(p - (origin + motion * along)) /
                std::max(radius, 1e-6f);
            if (normalized >= 1.0f) continue;
            const float coverage = bokeh ? 1.0f - normalized
                : std::clamp((1.0f - normalized) * radius + 0.5f, 0.0f, 1.0f);
            const Color src{particle.color.r, particle.color.g, particle.color.b,
                            alpha * coverage};
            row[x] = compositor::blend_normal(src, row[x]);
        }
    }
}
} // namespace

class SoftwareShapeProcessor final : public ShapeProcessor {
public:
    void draw(const SoftwareProcessorContext& rctx, Framebuffer& fb, const RenderNode& node, const RenderState& state,
              const Camera& camera, i32 width, i32 height) override {
        if (node.software_environment_particle) {
            draw_environment_particle(fb, node, state);
            return;
        }
        const Color linear_color = node.color.to_linear();
        Color fill_color = linear_color;
        fill_color.a *= state.opacity;

        const Fill* fill_ptr = (node.fill.type != FillType::Solid) ? &node.fill : nullptr;
        draw_transformed_shape(fb, node.shape, state.matrix, fill_color, 0.0f, &state, fill_ptr, node.corner_radius);
    }

    raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) override {
        return chronon3d::renderer::compute_world_bbox(shape, model, spread);
    }

    bool hit_test(const Shape& shape, Vec2 local_point, f32 spread) override {
        return chronon3d::renderer::hit_test(shape, local_point, spread);
    }
};

std::unique_ptr<ShapeProcessor> create_shape_processor() {
    return std::make_unique<SoftwareShapeProcessor>();
}

} // namespace chronon3d::renderer
