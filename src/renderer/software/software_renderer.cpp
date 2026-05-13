#include <chronon3d/renderer/software/software_renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/scene/scene.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/core/profiling.hpp>
#include "primitive_renderer.hpp"
#include "render_graph_builder.hpp"
#include "render_effects_processor.hpp"
#include <fmt/format.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace chronon3d {

using namespace renderer;

// ---------------------------------------------------------------------------
// SoftwareRenderer - Main Interface
// ---------------------------------------------------------------------------

std::unique_ptr<Framebuffer> SoftwareRenderer::render_frame(const Composition& comp, Frame frame) {
    if (!m_settings.motion_blur.enabled || m_settings.motion_blur.samples <= 1) {
        Scene scene = comp.evaluate(frame);
        return render_scene_internal(scene, comp.camera, comp.width(), comp.height(), frame, 0.0f);
    }

    const int   N       = std::max(2, m_settings.motion_blur.samples);
    const float shutter = m_settings.motion_blur.shutter_angle / 360.0f;
    const int   w       = comp.width();
    const int   h       = comp.height();

    std::vector<float> accum(static_cast<size_t>(w * h * 4), 0.0f);
    const float weight = 1.0f / static_cast<float>(N);

    for (int s = 0; s < N; ++s) {
        const float t = (static_cast<float>(s) / static_cast<float>(N)) * shutter;
        Scene sub_scene = comp.evaluate(frame, t);
        auto sub_fb = render_scene_internal(sub_scene, comp.camera, w, h, frame, t);

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                Color c = sub_fb->get_pixel(x, y);
                const size_t idx = static_cast<size_t>((y * w + x) * 4);
                accum[idx + 0] += c.r * weight;
                accum[idx + 1] += c.g * weight;
                accum[idx + 2] += c.b * weight;
                accum[idx + 3] += c.a * weight;
            }
        }
    }

    auto result = std::make_unique<Framebuffer>(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t idx = static_cast<size_t>((y * w + x) * 4);
            result->set_pixel(x, y, Color{accum[idx], accum[idx+1], accum[idx+2], accum[idx+3]});
        }
    }
    return result;
}

std::unique_ptr<Framebuffer> SoftwareRenderer::render_scene(
    const Scene& scene, const Camera& camera, i32 width, i32 height, Frame frame)
{
    return render_scene_internal(scene, camera, width, height, frame, 0.0f);
}

std::string SoftwareRenderer::debug_render_graph(const Scene& scene, const Camera& camera,
                                                 i32 width, i32 height, Frame frame,
                                                 f32 frame_time) const
{
    auto graph = build_render_graph(scene, camera, width, height, frame, frame_time);
    return graph.debug_dot();
}

std::unique_ptr<Framebuffer> SoftwareRenderer::render_scene_internal(
    const Scene& scene, const Camera& camera, i32 width, i32 height, Frame frame, f32 frame_time)
{
    ZoneScoped;

    if (m_settings.use_modular_graph) {
        graph::RenderGraphContext ctx;
        ctx.frame = frame;
        ctx.time_seconds = frame_time;
        ctx.width = width;
        ctx.height = height;
        ctx.camera = camera;
        ctx.renderer = this;
        ctx.node_cache = &m_node_cache;
        ctx.cache_enabled = true;
        ctx.diagnostics_enabled = m_settings.diagnostic;
        ctx.registry = m_registry;
        ctx.video_decoder = &m_video_extractor;

        auto graph = graph::GraphBuilder::build(scene, ctx);
        graph::GraphExecutor executor;
        auto result_shared = executor.execute(graph, ctx);

        if (!result_shared) return nullptr;
        return std::make_unique<Framebuffer>(*result_shared);
    }

    auto graph_wrapper = build_render_graph(scene, camera, width, height, frame, frame_time);

    rendergraph::RenderGraphExecutionContext ctx{
        .renderer = *this,
        .camera = camera,
        .frame = frame,
        .width = width,
        .height = height,
        .diagnostic = m_settings.diagnostic,
    };

    return graph_wrapper.execute(ctx);
}

rendergraph::RenderGraph SoftwareRenderer::build_render_graph(
    const Scene& scene, const Camera& camera, i32 width, i32 height, Frame frame, f32 frame_time) const
{
    return build_software_render_graph(*this, scene, camera, width, height, frame, frame_time);
}

// ---------------------------------------------------------------------------
// Node Drawing Dispatch
// ---------------------------------------------------------------------------

void SoftwareRenderer::draw_node(Framebuffer& fb, const RenderNode& node, const RenderState& state, 
                                 const Camera& camera, i32 width, i32 height) {
    const Color linear_color = node.color.to_linear();
    const Mat4& model = state.matrix;
    const f32 opacity = state.opacity;

    // Node-level effects
    if (node.shadow.enabled) renderer::draw_shadow(fb, node, state);
    if (node.glow.enabled)   renderer::draw_glow(fb, node, state);

    if (node.shape.type == ShapeType::Mesh) {
        if (node.mesh) {
            const f32 aspect = static_cast<f32>(width) / static_cast<f32>(height);
            renderer::render_mesh_wireframe(fb, *node.mesh, model, camera.view_matrix(), 
                                           camera.projection_matrix(aspect), linear_color);
        }
        return;
    }

    if (node.shape.type == ShapeType::Line) {
        Vec4 p0 = model * Vec4(0, 0, 0, 1);
        Vec4 p1 = model * Vec4(node.shape.line.to, 1);
        Color col = linear_color;
        col.a *= opacity;
        draw_line(fb, Vec3(p0.x, p0.y, p0.z), Vec3(p1.x, p1.y, p1.z), col);
        return;
    }

    if (node.shape.type == ShapeType::Text) {
        Transform text_tr;
        text_tr.position = Vec3(model[3]);
        text_tr.opacity  = opacity;
        text_tr.scale.x  = glm::length(Vec3(model[0]));
        text_tr.scale.y  = glm::length(Vec3(model[1]));
        m_text_renderer.draw_text(node.shape.text, text_tr, fb, &state);
        return;
    }

    if (node.shape.type == ShapeType::Image) {
        m_image_renderer.draw_image(node.shape.image, state, fb);
        return;
    }

    if (node.shape.type == ShapeType::FakeBox3D) {
        renderer::draw_fake_box3d(fb, node, state);
        return;
    }

    if (node.shape.type == ShapeType::GridPlane) {
        renderer::draw_grid_plane(fb, node, state);
        return;
    }

    // Standard 2D Shape
    Color fill_color = linear_color;
    fill_color.a *= opacity;
    renderer::draw_transformed_shape(fb, node.shape, model, fill_color, 0.0f, &state);

    if (m_settings.diagnostic) {
        draw_diagnostic_info(fb, node, state);
    }
}

void SoftwareRenderer::draw_diagnostic_info(Framebuffer& fb, const RenderNode& node, const RenderState& state) {
    TextStyle debug_style;
    debug_style.font_path = "assets/fonts/Inter-Regular.ttf";
    debug_style.size = 12.0f;
    debug_style.color = Color{1, 0, 0, 0.8f};

    TextShape debug_text;
    debug_text.text = fmt::format("{}: ({:.1f}, {:.1f})", std::string(node.name), state.matrix[3][0], state.matrix[3][1]);
    debug_text.style = debug_style;

    Transform text_tr;
    text_tr.position = Vec3(state.matrix[3]);
    m_text_renderer.draw_text(debug_text, text_tr, fb);
}

void SoftwareRenderer::draw_line(Framebuffer& fb, const Vec3& p1, const Vec3& p2, const Color& color) {
    renderer::bline(fb, Vec2(p1.x, p1.y), Vec2(p2.x, p2.y), color);
}

// ---------------------------------------------------------------------------
// Static Helpers (Effects & Compositing)
// ---------------------------------------------------------------------------

void SoftwareRenderer::apply_blur(Framebuffer& fb, f32 radius) {
    renderer::apply_blur(fb, radius);
}

void SoftwareRenderer::apply_effect_stack(Framebuffer& fb, const EffectStack& stack) {
    renderer::apply_effect_stack(fb, stack);
}

void SoftwareRenderer::composite_layer(Framebuffer& dst, const Framebuffer& src, BlendMode mode) {
    const i32 w = dst.width(), h = dst.height();
    for (i32 y = 0; y < h; ++y) {
        for (i32 x = 0; x < w; ++x) {
            Color s = src.get_pixel(x, y);
            if (s.a <= 0.0f) continue;
            s = s.unpremultiplied();
            dst.set_pixel(x, y, compositor::blend(s, dst.get_pixel(x, y), mode));
        }
    }
}

// Private dummy for internal usage if needed
void SoftwareRenderer::apply_color_effects(Framebuffer& fb, const LayerEffect& effect) {
    renderer::apply_color_effects(fb, effect);
}

void SoftwareRenderer::draw_shadow(Framebuffer& fb, const RenderNode& node, const RenderState& state) {
    renderer::draw_shadow(fb, node, state);
}

void SoftwareRenderer::draw_glow(Framebuffer& fb, const RenderNode& node, const RenderState& state) {
    renderer::draw_glow(fb, node, state);
}

void SoftwareRenderer::render_layer_nodes(Framebuffer& fb, const Layer& layer,
                                          const RenderState& layer_state,
                                          const Camera& camera, i32 width, i32 height) {
    for (const auto& node : layer.nodes) {
        if (!node.visible) continue;
        RenderState node_state = combine(layer_state, node.world_transform);
        draw_node(fb, node, node_state, camera, width, height);
    }
}

void SoftwareRenderer::render_mesh_wireframe(Framebuffer& fb, const Mesh& mesh, const Mat4& model,
                                             const Mat4& view, const Mat4& proj, const Color& color) {
    renderer::render_mesh_wireframe(fb, mesh, model, view, proj, color);
}

} // namespace chronon3d
