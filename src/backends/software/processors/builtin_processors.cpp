#include <chronon3d/backends/software/builtin_processors.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/software/effect_processor.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include "../rasterizers/shape_rasterizer.hpp"
#include "../primitive_renderer.hpp"
#include "../utils/render_effects_processor.hpp"
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/compositor/blend_mode.hpp>

namespace chronon3d::renderer {

// --- Shapes ---

class StandardShapeProcessor final : public ShapeProcessor {
public:
    void draw(Framebuffer& fb, const RenderNode& node, const RenderState& state, 
              const Camera& camera, i32 width, i32 height) override {
        const Color linear_color = node.color.to_linear();
        Color fill_color = linear_color;
        fill_color.a *= state.opacity;
        draw_transformed_shape(fb, node.shape, state.matrix, fill_color, 0.0f, &state);
    }

    raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) override {
        return chronon3d::renderer::compute_world_bbox(shape, model, spread);
    }

    bool hit_test(const Shape& shape, Vec2 local_point, f32 spread) override {
        return chronon3d::renderer::hit_test(shape, local_point, spread);
    }
};

class LineProcessor final : public ShapeProcessor {
public:
    void draw(Framebuffer& fb, const RenderNode& node, const RenderState& state, 
              const Camera& camera, i32 width, i32 height) override {
        Vec4 p0 = state.matrix * Vec4(0, 0, 0, 1);
        Vec4 p1 = state.matrix * Vec4(node.shape.line.to, 1);
        Color col = node.color.to_linear();
        col.a *= state.opacity;
        bline(fb, Vec2(p0.x, p0.y), Vec2(p1.x, p1.y), col);
    }

    raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) override {
        Vec4 p0 = model * Vec4(0, 0, 0, 1);
        Vec4 p1 = model * Vec4(shape.line.to, 1);
        return {
            static_cast<i32>(std::floor(std::min(p0.x, p1.x) - spread)),
            static_cast<i32>(std::floor(std::min(p0.y, p1.y) - spread)),
            static_cast<i32>(std::ceil(std::max(p0.x, p1.x) + spread)),
            static_cast<i32>(std::ceil(std::max(p0.y, p1.y) + spread))
        };
    }

    bool hit_test(const Shape& shape, Vec2 local_point, f32 spread) override {
        return false; 
    }
};

class TextProcessor final : public ShapeProcessor {
public:
    void draw(Framebuffer& fb, const RenderNode& node, const RenderState& state, 
              const Camera& camera, i32 width, i32 height) override {
        // We need access to the SoftwareRenderer to get the TextRenderer.
        // For now we'll assume we can cast or we'll need to pass more context.
        // Actually, RenderState doesn't have it.
        // I'll update draw_node to pass the renderer or use a callback.
    }
    raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) override { return {0,0,0,0}; }
    bool hit_test(const Shape& shape, Vec2 local_point, f32 spread) override { return false; }
};

// --- Effects ---

class BlurEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params) override {
        if (auto* p = std::get_if<BlurParams>(&params)) {
            apply_blur(fb, p->radius);
        }
    }
};

class TintEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params) override {
        if (auto* p = std::get_if<TintParams>(&params)) {
            LayerEffect e;
            e.tint = Color{p->color.r, p->color.g, p->color.b, p->color.a * p->amount};
            apply_color_effects(fb, e);
        }
    }
};

void register_builtin_processors(SoftwareRegistry& registry) {
    registry.register_shape(ShapeType::Rect, std::make_unique<StandardShapeProcessor>());
    registry.register_shape(ShapeType::Circle, std::make_unique<StandardShapeProcessor>());
    registry.register_shape(ShapeType::RoundedRect, std::make_unique<StandardShapeProcessor>());
    registry.register_shape(ShapeType::Line, std::make_unique<LineProcessor>());
    
    registry.register_effect_processor<BlurParams>(std::make_unique<BlurEffectProcessor>());
    registry.register_effect_processor<TintParams>(std::make_unique<TintEffectProcessor>());
}

} // namespace chronon3d::renderer
