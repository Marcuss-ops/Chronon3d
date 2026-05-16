#include <chronon3d/backends/software/software_renderer.hpp>

#include "primitive_renderer.hpp"
#include "utils/render_effects_processor.hpp"

#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/backends/software/fake_extruded_text_renderer.hpp>
#include <chronon3d/scene/camera/camera.hpp>
#include <filesystem>
#include <fmt/format.h>

#include "utils/blend2d_bridge.hpp"
#include "utils/blend2d_resources.hpp"

namespace chronon3d::software_internal {

void draw_node(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node,
               const RenderState& state, const Camera& camera, i32 width, i32 height) {
    const Color linear_color = node.color.to_linear();
    const Mat4& model = state.matrix;
    const f32 opacity = state.opacity;

    chronon3d::renderer::draw_shadow(fb, node, state);

    if (node.glow.enabled && node.glow.intensity > 0.0f && node.glow.color.a > 0.0f) {
        // OPTIMIZED GLOW: Blend2D Render -> Blur -> Bridge Add
        const f32 glow_opacity = opacity * node.glow.intensity * node.glow.color.a;

        if (node.shape.type == ShapeType::Text) {
            const TextShape& t = node.shape.text;
            BLFontFace face = blend2d_utils::Blend2DResources::instance().get_face(t.style.font_path);
            if (!face.empty()) {
                const float scale_x = glm::length(Vec3(model[0]));
                const float effective_size = t.style.size * scale_x;
                BLFont font;
                font.createFromFace(face, effective_size);

                BLGlyphBuffer gb;
                gb.setUtf8Text(t.text.c_str(), t.text.size());
                font.shape(gb);

                BLTextMetrics metrics;
                font.getTextMetrics(gb, metrics);

                const int padding = static_cast<int>(node.glow.radius * 2.0f) + 10;
                const int tw = static_cast<int>(std::ceil(metrics.boundingBox.x1 - metrics.boundingBox.x0)) + padding * 2;
                const int th = static_cast<int>(std::ceil(font.metrics().ascent + font.metrics().descent)) + padding * 2;

                if (tw > 0 && th > 0) {
                    BLImage img(tw, th, BL_FORMAT_PRGB32);
                    BLContext ctx(img);
                    ctx.clearAll();
                    
                    // Render glow in white on the temp buffer, we'll tint it during composition if needed
                    // or just render in glow color.
                    BLRgba32 bl_color(
                        static_cast<uint8_t>(std::clamp(node.glow.color.r * 255.0f, 0.0f, 255.0f)),
                        static_cast<uint8_t>(std::clamp(node.glow.color.g * 255.0f, 0.0f, 255.0f)),
                        static_cast<uint8_t>(std::clamp(node.glow.color.b * 255.0f, 0.0f, 255.0f)),
                        255
                    );
                    ctx.setFillStyle(bl_color);
                    ctx.fillUtf8Text(BLPoint(-metrics.boundingBox.x0 + padding, font.metrics().ascent + padding), font, t.text.c_str());
                    ctx.end();

                    // Fallback blur: Transfer to temp Framebuffer and use existing blur
                    Framebuffer glow_tmp(tw, th);
                    glow_tmp.clear(Color::transparent());
                    blend2d_bridge::composite_bl_image(glow_tmp, img, 0, 0, 1.0f, BlendMode::Normal);
                    
                    if (node.glow.radius > 0.0f) {
                        SoftwareRenderer::apply_blur(glow_tmp, node.glow.radius);
                    }

                    float x_offset = 0.0f;
                    if (t.style.align == TextAlign::Center) x_offset = -metrics.advance.x * 0.5f;
                    else if (t.style.align == TextAlign::Right) x_offset = -metrics.advance.x;
                    x_offset += metrics.boundingBox.x0 - padding;

                    float y_offset = -font.metrics().ascent - padding;
                    if (t.style.align == TextAlign::Center) {
                        y_offset += (font.metrics().ascent - font.metrics().descent) * 0.5f;
                    }

                    // Composite from temp FB to main FB with ADD
                    blend2d_bridge::composite_framebuffer_offset(fb, glow_tmp, 
                        static_cast<int>(model[3][0] + x_offset), 
                        static_cast<int>(model[3][1] + y_offset), 
                        glow_opacity, BlendMode::Add);
                }
            }
        }
    }

    if (node.shape.type == ShapeType::Mesh) {
        if (node.mesh) {
            const f32 aspect = static_cast<f32>(width) / static_cast<f32>(height);
            chronon3d::renderer::render_mesh_wireframe(
                fb, *node.mesh, model, camera.view_matrix(),
                camera.projection_matrix(aspect), linear_color);
        }
        return;
    }

    if (node.shape.type == ShapeType::Line) {
        Vec4 p0 = model * Vec4(0, 0, 0, 1);
        Vec4 p1 = model * Vec4(node.shape.line.to, 1);
        Color col = linear_color;
        col.a *= opacity;
        chronon3d::renderer::bline(fb, Vec2(p0.x, p0.y), Vec2(p1.x, p1.y), col);
        return;
    }

    if (node.shape.type == ShapeType::Text) {
        Transform text_tr;
        text_tr.position = Vec3(model[3]);
        text_tr.opacity = opacity;
        text_tr.scale.x = glm::length(Vec3(model[0]));
        text_tr.scale.y = glm::length(Vec3(model[1]));

        float y_offset = -metrics.ascent - 2;
        if (node.shape.text.style.align == TextAlign::Center) {
            y_offset += (metrics.ascent - metrics.descent) * 0.5f;
        }

        text_tr.position.y += y_offset;
        renderer.text_renderer().draw_text(node.shape.text, text_tr, fb, &state);
        return;
    }

    if (node.shape.type == ShapeType::Image) {
        renderer.image_renderer().draw_image(node.shape.image, state, fb);
        return;
    }

    if (node.shape.type == ShapeType::FakeExtrudedText) {
        renderer.fake_extruded_text_renderer().draw(
            fb, node, state, camera, width, height, renderer.text_renderer());
        return;
    }

    if (node.shape.type == ShapeType::FakeBox3D) {
        auto s = node.fake_box3d_runtime;
        if (!s.cam_ready) {
            s.cam_ready = true;
            s.cam_view = camera.view_matrix();
            const f32 fw = static_cast<f32>(width);
            s.cam_focal = camera.focal_length(fw);
            s.vp_cx = fw * 0.5f;
            s.vp_cy = static_cast<f32>(height) * 0.5f;
        }
        chronon3d::renderer::draw_fake_box3d(fb, node, state, node.shape.fake_box3d, s);
        return;
    }

    if (node.shape.type == ShapeType::GridPlane) {
        auto s = node.grid_plane_runtime;
        if (!s.cam_ready) {
            s.cam_ready = true;
            s.cam_view = camera.view_matrix();
            const f32 fw = static_cast<f32>(width);
            s.cam_focal = camera.focal_length(fw);
            s.vp_cx = fw * 0.5f;
            s.vp_cy = static_cast<f32>(height) * 0.5f;
        }
        chronon3d::renderer::draw_grid_plane(fb, node, state, node.shape.grid_plane, s);
        return;
    }

    Color fill_color = linear_color;
    fill_color.a *= opacity;
    chronon3d::renderer::draw_transformed_shape(fb, node.shape, model, fill_color, 0.0f, &state);

    const RenderSettings& settings = renderer.render_settings();
    if (settings.diagnostic) {
        TextStyle debug_style;
        debug_style.font_path = settings.diagnostic_font_path.empty()
                                 ? "assets/fonts/Inter-Regular.ttf"
                                 : settings.diagnostic_font_path;

        if (!std::filesystem::exists(debug_style.font_path)) {
            fb.set_pixel(static_cast<int>(state.matrix[3][0]), static_cast<int>(state.matrix[3][1]),
                         Color{1, 0, 0, 1});
            return;
        }

        debug_style.size = 12.0f;
        debug_style.color = Color{1, 0, 0, 0.8f};

        TextShape debug_text;
        debug_text.text = fmt::format("{}: ({:.1f}, {:.1f})", std::string(node.name),
                                      state.matrix[3][0], state.matrix[3][1]);
        debug_text.style = debug_style;

        Transform text_tr;
        text_tr.position = Vec3(state.matrix[3]);
        renderer.text_renderer().draw_text(debug_text, text_tr, fb);
    }
}

} // namespace chronon3d::software_internal
