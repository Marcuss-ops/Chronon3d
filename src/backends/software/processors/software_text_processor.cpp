#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/text/text_rasterizer_utils.hpp>
#include "../utils/render_effects_processor.hpp"
#include "../utils/blend2d_bridge.hpp"
#include "../utils/blend2d_resources.hpp"
#include <blend2d.h>
#include <glm/gtc/matrix_transform.hpp>

namespace chronon3d::renderer {

class SoftwareTextProcessor final : public ShapeProcessor {
public:
    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state,
              const Camera& camera, i32 width, i32 height) override {
        const Mat4& model = state.matrix;
        const f32 opacity = state.opacity;

        // 1. Shadow
        draw_shadow(fb, node, state);

        // 2. Optimized Glow (Blend2D)
        if (node.glow.enabled && node.glow.intensity > 0.0f && node.glow.color.a > 0.0f) {
            draw_text_glow(fb, node, state, renderer);
        }

        // 3. Text
        Transform text_tr;
        text_tr.position = Vec3(model[3]);
        text_tr.opacity = opacity;
        text_tr.scale.x = glm::length(Vec3(model[0]));
        text_tr.scale.y = glm::length(Vec3(model[1]));

        // Single source of truth: TextRenderer handles all vertical offsets
        renderer.text_renderer().draw_text(node.shape.text, text_tr, fb, &state);
    }

    raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) override {
        // Simple approximation for text bbox
        Vec3 pos = Vec3(model[3]);
        return {
            static_cast<i32>(pos.x - 200),
            static_cast<i32>(pos.y - 100),
            static_cast<i32>(pos.x + 200),
            static_cast<i32>(pos.y + 100)
        };
    }

    bool hit_test(const Shape& shape, Vec2 local_point, f32 spread) override {
        return false;
    }

private:
    void draw_text_glow(Framebuffer& fb, const RenderNode& node, const RenderState& state, 
                        SoftwareRenderer& renderer) {
        const TextShape& t = node.shape.text;
        const Mat4& model = state.matrix;
        const f32 opacity = state.opacity;

        const float scale_x = glm::length(Vec3(model[0]));
        const float effective_size = t.style.size * scale_x;
        
        // Use a larger padding for glow to avoid clipping the blur
        const int glow_padding = static_cast<int>(node.glow.radius * 2.0f) + 16;
        auto raster = rasterize_text_to_bl_image(t, effective_size, glow_padding);
        if (!raster) return;

        // Apply glow color
        BLContext ctx(raster->image);
        ctx.setCompOp(BL_COMP_OP_SRC_IN);
        ctx.setFillStyle(BLRgba32(
            static_cast<uint8_t>(std::clamp(node.glow.color.r * 255.0f, 0.0f, 255.0f)),
            static_cast<uint8_t>(std::clamp(node.glow.color.g * 255.0f, 0.0f, 255.0f)),
            static_cast<uint8_t>(std::clamp(node.glow.color.b * 255.0f, 0.0f, 255.0f)),
            255
        ));
        ctx.fillAll();
        ctx.end();

        Framebuffer glow_tmp(raster->image.width(), raster->image.height());
        glow_tmp.clear(Color::transparent());
        blend2d_bridge::composite_bl_image(glow_tmp, raster->image, 0, 0, 1.0f, BlendMode::Normal);
        
        if (node.glow.radius > 0.0f) {
            SoftwareRenderer::apply_blur(glow_tmp, node.glow.radius);
        }

        const f32 glow_opacity = opacity * node.glow.intensity * node.glow.color.a;

        blend2d_bridge::composite_framebuffer_offset(fb, glow_tmp, 
            static_cast<int>(model[3][0] + raster->x_offset), 
            static_cast<int>(model[3][1] + raster->y_offset), 
            glow_opacity, BlendMode::Add);
    }
};

std::unique_ptr<ShapeProcessor> create_text_processor() {
    return std::make_unique<SoftwareTextProcessor>();
}

} // namespace chronon3d::renderer
