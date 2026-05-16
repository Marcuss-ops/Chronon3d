#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/software/rasterizers/projected_card_rasterizer.hpp>
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

        const float effective_size = node.shape.text.style.size;

        // 3D card path: rasterize text to offscreen BLImage, then project as card
        if (state.is_3d_layer && state.projection.ready) {
            auto raster = rasterize_text_to_bl_image(node.shape.text, effective_size);
            if (!raster) return;

            // Apply text color
            BLImage text_img;
            text_img.create(raster->image.width(), raster->image.height(), BL_FORMAT_PRGB32);
            {
                BLContext bctx(text_img);
                bctx.clearAll();
                bctx.blitImage(BLPoint(0, 0), raster->image);
                bctx.setCompOp(BL_COMP_OP_SRC_IN);
                bctx.setFillStyle(BLRgba32(
                    static_cast<uint8_t>(std::clamp(node.shape.text.style.color.r * 255.0f, 0.0f, 255.0f)),
                    static_cast<uint8_t>(std::clamp(node.shape.text.style.color.g * 255.0f, 0.0f, 255.0f)),
                    static_cast<uint8_t>(std::clamp(node.shape.text.style.color.b * 255.0f, 0.0f, 255.0f)),
                    255
                ));
                bctx.fillAll();
            }

            // Convert BLImage to Framebuffer and project as card
            Framebuffer text_fb(raster->image.width(), raster->image.height());
            text_fb.clear(Color::transparent());
            blend2d_bridge::composite_bl_image(text_fb, text_img, 0, 0, 1.0f, BlendMode::Normal);

            const Vec2 text_size{
                static_cast<f32>(raster->image.width()),
                static_cast<f32>(raster->image.height())
            };
            auto card = state.projection.project_card(state.world_matrix, text_size);
            composite_projected_framebuffer(fb, text_fb, card, opacity);
            return;
        }
        
        // 1. Rasterize text once for both glow and main text
        auto raster = rasterize_text_to_bl_image(node.shape.text, effective_size);
        if (!raster) return;

        // 2. Glow
        if (node.glow.enabled && node.glow.intensity > 0.0f && node.glow.color.a > 0.0f) {
            draw_text_glow(fb, node, state, *raster);
        }

        // 3. Text
        // Apply text color to a copy of the raster
        BLImage text_img;
        text_img.create(raster->image.width(), raster->image.height(), BL_FORMAT_PRGB32);
        {
            BLContext ctx(text_img);
            ctx.clearAll();
            ctx.blitImage(BLPoint(0, 0), raster->image);
            ctx.setCompOp(BL_COMP_OP_SRC_IN);
            ctx.setFillStyle(BLRgba32(
                static_cast<uint8_t>(std::clamp(node.shape.text.style.color.r * 255.0f, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(node.shape.text.style.color.g * 255.0f, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(node.shape.text.style.color.b * 255.0f, 0.0f, 255.0f)),
                255
            ));
            ctx.fillAll();
        }

        // Use the transformed compositor to respect perspective/tilt
        Mat4 text_model = model * glm::translate(Mat4(1.0f), Vec3(raster->x_offset, raster->y_offset, 0.0f));
        
        blend2d_bridge::composite_bl_image_transformed(fb, text_img, text_model, opacity, BlendMode::Normal);
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
    void draw_text_glow(Framebuffer& fb, const RenderNode& node, const RenderState& state, const TextRasterization& raster) {
        const Mat4& model = state.matrix;
        const f32 opacity = state.opacity;
        // Apply glow color to a copy of the text image
        BLImage glow_img;
        glow_img.create(raster.image.width(), raster.image.height(), BL_FORMAT_PRGB32);
        {
            BLContext ctx(glow_img);
            ctx.clearAll();
            ctx.blitImage(BLPoint(0, 0), raster.image);
            ctx.setCompOp(BL_COMP_OP_SRC_IN);
            ctx.setFillStyle(BLRgba32(
                static_cast<uint8_t>(std::clamp(node.glow.color.r * 255.0f, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(node.glow.color.g * 255.0f, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(node.glow.color.b * 255.0f, 0.0f, 255.0f)),
                255
            ));
            ctx.fillAll();
        }

        // We blur in a temporary framebuffer to use the existing software blur
        Framebuffer glow_tmp(glow_img.width(), glow_img.height());
        glow_tmp.clear(Color::transparent());
        blend2d_bridge::composite_bl_image(glow_tmp, glow_img, 0, 0, 1.0f, BlendMode::Normal);
        
        if (node.glow.radius > 0.0f) {
            SoftwareRenderer::apply_blur(glow_tmp, node.glow.radius);
        }

        const f32 glow_intensity_opacity = node.glow.intensity * node.glow.color.a;

        // Use transformed compositor to follow perspective
        Mat4 glow_model = model * glm::translate(Mat4(1.0f), Vec3(raster.x_offset, raster.y_offset, 0.0f));
        
        blend2d_bridge::composite_framebuffer_transformed(fb, glow_tmp, glow_model, opacity * glow_intensity_opacity, BlendMode::Add);
    }
};

std::unique_ptr<ShapeProcessor> create_text_processor() {
    return std::make_unique<SoftwareTextProcessor>();
}

} // namespace chronon3d::renderer
