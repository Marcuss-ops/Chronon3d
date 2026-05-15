#include <chronon3d/backends/software/software_renderer.hpp>

#include "primitive_renderer.hpp"
#include "utils/render_effects_processor.hpp"

#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/backends/software/fake_extruded_text_renderer.hpp>
#include <chronon3d/scene/camera/camera.hpp>
#include <chronon3d/scene/scene.hpp>
#include <filesystem>
#include <fmt/format.h>

namespace chronon3d::software_internal {

void draw_node(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node,
               const RenderState& state, const Camera& camera, i32 width, i32 height) {
    const Color linear_color = node.color.to_linear();
    const Mat4& model = state.matrix;
    const f32 opacity = state.opacity;

    if (node.shadow.enabled)
        chronon3d::renderer::draw_shadow(fb, node, state);
    if (node.glow.enabled)
        chronon3d::renderer::draw_glow(fb, node, state);

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
        if (node.glow.enabled && node.glow.color.a > 0.0f && node.glow.intensity > 0.0f) {
            Framebuffer glow_fb(fb.width(), fb.height());
            glow_fb.clear(Color::transparent());

            TextShape glow_text = node.shape.text;
            glow_text.style.color = node.glow.color.to_linear();

            Transform glow_tr;
            glow_tr.position = Vec3(model[3]);
            glow_tr.opacity = opacity * node.glow.intensity;
            glow_tr.scale.x = glm::length(Vec3(model[0]));
            glow_tr.scale.y = glm::length(Vec3(model[1]));

            renderer.text_renderer().draw_text(glow_text, glow_tr, glow_fb, &state);
            if (node.glow.radius > 0.0f) {
                SoftwareRenderer::apply_blur(glow_fb, node.glow.radius);
            }
            SoftwareRenderer::composite_layer(fb, glow_fb, BlendMode::Normal);
        }

        Transform text_tr;
        text_tr.position = Vec3(model[3]);
        text_tr.opacity = opacity;
        text_tr.scale.x = glm::length(Vec3(model[0]));
        text_tr.scale.y = glm::length(Vec3(model[1]));
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
