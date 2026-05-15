#include <chronon3d/renderer/software/software_renderer.hpp>

#include "primitive_renderer.hpp"
#include "utils/render_effects_processor.hpp"

#include <algorithm>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/core/profiling.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/renderer/software/fake_extruded_text_renderer.hpp>
#include <chronon3d/scene/camera/camera.hpp>
#include <chronon3d/scene/scene.hpp>
#include <cmath>
#include <filesystem>
#include <fmt/format.h>
#include <vector>

namespace chronon3d::software_internal {

using namespace chronon3d::renderer;

namespace {

std::unique_ptr<Framebuffer> downsample_fb_local(const Framebuffer& src, i32 dst_w, i32 dst_h) {
    auto dst = std::make_unique<Framebuffer>(dst_w, dst_h);
    const float sx = static_cast<float>(src.width()) / static_cast<float>(dst_w);
    const float sy = static_cast<float>(src.height()) / static_cast<float>(dst_h);

    for (int y = 0; y < dst_h; ++y) {
        for (int x = 0; x < dst_w; ++x) {
            float r = 0, g = 0, b = 0, a = 0;
            int count = 0;
            int x0 = static_cast<int>(x * sx);
            int y0 = static_cast<int>(y * sy);
            int x1 = std::min(static_cast<int>((x + 1) * sx), src.width());
            int y1 = std::min(static_cast<int>((y + 1) * sy), src.height());

            for (int sy_i = y0; sy_i < y1; ++sy_i) {
                for (int sx_i = x0; sx_i < x1; ++sx_i) {
                    Color c = src.get_pixel(sx_i, sy_i);
                    r += c.r;
                    g += c.g;
                    b += c.b;
                    a += c.a;
                    count++;
                }
            }
            if (count > 0) {
                float inv = 1.0f / count;
                dst->set_pixel(x, y, Color{r * inv, g * inv, b * inv, a * inv});
            }
        }
    }
    return dst;
}

graph::RenderGraphContext make_graph_context(SoftwareRenderer& renderer, const Camera& camera,
                                             i32 width, i32 height, Frame frame,
                                             f32 frame_time) {
    return graph::RenderGraphContext{
        .frame = frame,
        .time_seconds = frame_time,
        .width = width,
        .height = height,
        .camera = camera,
        .renderer = &renderer,
        .node_cache = &renderer.node_cache(),
        .registry = renderer.composition_registry(),
        .ssaa_factor = renderer.render_settings().ssaa_factor,
        .modular_coordinates = renderer.render_settings().use_modular_graph
    };
}

} // namespace

std::unique_ptr<Framebuffer> render_frame(SoftwareRenderer& renderer, const Composition& comp,
                                          Frame frame) {
    const RenderSettings& settings = renderer.render_settings();
    const float ssaa = std::max(1.0f, settings.ssaa_factor);
    const int w = comp.width();
    const int h = comp.height();
    const int rw = static_cast<int>(w * ssaa);
    const int rh = static_cast<int>(h * ssaa);

    std::unique_ptr<Framebuffer> render_fb;

    if (!settings.motion_blur.enabled || settings.motion_blur.samples <= 1) {
        Scene scene = comp.evaluate(frame);
        render_fb = render_scene_internal(renderer, scene, comp.camera, rw, rh, frame, 0.0f);
    } else {
        const int N = std::max(2, settings.motion_blur.samples);
        const float shutter = settings.motion_blur.shutter_angle / 360.0f;

        std::vector<float> accum(static_cast<size_t>(rw * rh * 4), 0.0f);
        const float weight = 1.0f / static_cast<float>(N);

        for (int s = 0; s < N; ++s) {
            const float t = (static_cast<float>(s) / static_cast<float>(N)) * shutter;
            Scene sub_scene = comp.evaluate(frame, t);
            auto sub_fb = render_scene_internal(renderer, sub_scene, comp.camera, rw, rh, frame, t);

            for (int y = 0; y < rh; ++y) {
                for (int x = 0; x < rw; ++x) {
                    Color c = sub_fb->get_pixel(x, y);
                    const size_t idx = static_cast<size_t>((y * rw + x) * 4);
                    accum[idx + 0] += c.r * weight;
                    accum[idx + 1] += c.g * weight;
                    accum[idx + 2] += c.b * weight;
                    accum[idx + 3] += c.a * weight;
                }
            }
        }

        render_fb = std::make_unique<Framebuffer>(rw, rh);
        for (int y = 0; y < rh; ++y) {
            for (int x = 0; x < rw; ++x) {
                const size_t idx = static_cast<size_t>((y * rw + x) * 4);
                render_fb->set_pixel(
                    x, y, Color{accum[idx], accum[idx + 1], accum[idx + 2], accum[idx + 3]});
            }
        }
    }

    if (ssaa > 1.0f) {
        return downsample_fb_local(*render_fb, w, h);
    }
    return render_fb;
}

std::unique_ptr<Framebuffer> render_scene_internal(SoftwareRenderer& renderer,
                                                   const Scene& scene, const Camera& camera,
                                                   i32 width, i32 height, Frame frame,
                                                   f32 frame_time) {
    ZoneScoped;

    auto ctx = make_graph_context(renderer, camera, width, height, frame, frame_time);
    graph::RenderGraph graph = graph::GraphBuilder::build(scene, ctx);
    graph::GraphExecutor executor;
    auto fb_shared = executor.execute(graph, graph.output(), ctx);
    return std::make_unique<Framebuffer>(*fb_shared);
}

std::string debug_render_graph(const SoftwareRenderer& renderer, const Scene& scene,
                               const Camera& camera, i32 width, i32 height, Frame frame,
                               f32 frame_time) {
    auto ctx = make_graph_context(const_cast<SoftwareRenderer&>(renderer), camera, width, height,
                                  frame, frame_time);
    return graph::GraphBuilder::build(scene, ctx).to_dot();
}

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
