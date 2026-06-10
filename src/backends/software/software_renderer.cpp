#include "utils/render_effects_processor.hpp"
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/backends/software/software_compositor.hpp>
#include <chronon3d/backends/software/software_effect_runner.hpp>
#include <chronon3d/backends/software/software_node_dispatcher.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/software/builtin_processors.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/core/enum_utils.hpp>
#include <optional>
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>
#include "rasterizers/line_rasterizer.hpp"
#include "rasterizers/shape_rasterizer.hpp"
#include <algorithm>
#include <cmath>
#include <string_view>
#include <chronon3d/math/camera_2_5d_projection.hpp>

namespace chronon3d {
    struct RenderNode;
    struct RenderState;
    namespace raster { struct BBox; }
}

namespace chronon3d::renderer {

void apply_blur(Framebuffer& fb, f32 radius, const std::optional<raster::BBox>& clip, int passes);
void apply_color_effects(Framebuffer& fb, const LayerEffect& effect, const std::optional<raster::BBox>& clip);
void apply_effect_stack(Framebuffer& fb, const EffectStack& stack, float time_seconds, const std::optional<raster::BBox>& clip, bool diagnostics_enabled);
}

namespace chronon3d {

namespace {

std::optional<raster::BBox> to_local_clip(const Framebuffer& fb, std::optional<raster::BBox> clip) {
    if (!clip) {
        return std::nullopt;
    }

    raster::BBox c = *clip;
    c.x0 -= fb.origin_x();
    c.x1 -= fb.origin_x();
    c.y0 -= fb.origin_y();
    c.y1 -= fb.origin_y();
    return c;
}

uint64_t clipped_area(int32_t width, int32_t height, const std::optional<raster::BBox>& clip) {
    uint64_t area = static_cast<uint64_t>(std::max(0, width)) * static_cast<uint64_t>(std::max(0, height));
    if (!clip) {
        return area;
    }

    raster::BBox local_clip = *clip;
    local_clip.clip_to(width, height);
    if (local_clip.is_empty()) {
        return 0;
    }
    return static_cast<uint64_t>(std::max(0, local_clip.x1 - local_clip.x0)) *
           static_cast<uint64_t>(std::max(0, local_clip.y1 - local_clip.y0));
}

void draw_crosshair(Framebuffer& fb, Vec2 center, f32 radius, const Color& color) {
    renderer::bline(fb, {center.x - radius, center.y}, {center.x + radius, center.y}, color);
    renderer::bline(fb, {center.x, center.y - radius}, {center.x, center.y + radius}, color);
}

void draw_bbox_overlay(Framebuffer& fb, const raster::BBox& bbox, const Color& color) {
    if (bbox.is_empty()) {
        return;
    }

    const f32 x0 = static_cast<f32>(bbox.x0);
    const f32 y0 = static_cast<f32>(bbox.y0);
    const f32 x1 = static_cast<f32>(std::max(bbox.x0, bbox.x1 - 1));
    const f32 y1 = static_cast<f32>(std::max(bbox.y0, bbox.y1 - 1));

    renderer::bline(fb, {x0, y0}, {x1, y0}, color);
    renderer::bline(fb, {x1, y0}, {x1, y1}, color);
    renderer::bline(fb, {x1, y1}, {x0, y1}, color);
    renderer::bline(fb, {x0, y1}, {x0, y0}, color);
}

const char* shape_type_name(ShapeType type) {
    static thread_local std::string name;
    name = std::string(enum_utils::enum_name_exact(type));
    return name.empty() ? "Unknown" : name.c_str();
}

void draw_layout_preview(Framebuffer& fb, const RenderNode& node, const RenderState& state,
                         renderer::ShapeProcessor& processor) {
    const auto bbox = processor.compute_world_bbox(node.shape, state.matrix, 0.0f);
    draw_bbox_overlay(fb, bbox, Color{0.15f, 0.90f, 1.0f, 0.95f});

    Vec4 anchor = state.matrix * Vec4(node.world_transform.anchor, 1.0f);
    if (std::abs(anchor.w) > 1e-7f) {
        draw_crosshair(fb, {anchor.x / anchor.w, anchor.y / anchor.w}, 6.0f,
                       Color{1.0f, 0.20f, 0.72f, 0.95f});
    }

    draw_crosshair(
        fb,
        {static_cast<f32>(fb.width()) * 0.5f, static_cast<f32>(fb.height()) * 0.5f},
        16.0f,
        Color{1.0f, 1.0f, 1.0f, 0.55f}
    );

    const Vec4 world_pos = state.matrix * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    Vec2 anchor_screen{0.0f, 0.0f};
    if (std::abs(anchor.w) > 1e-7f) {
        anchor_screen = {anchor.x / anchor.w, anchor.y / anchor.w};
    }

    spdlog::info(
        "[layout-preview] node='{}' type={} pos=({:.1f},{:.1f},{:.1f}) anchor=({:.1f},{:.1f},{:.1f}) bbox=[{},{},{},{}] screen_anchor=({:.1f},{:.1f})",
        node.name,
        shape_type_name(node.shape.type),
        world_pos.x,
        world_pos.y,
        world_pos.z,
        node.world_transform.anchor.x,
        node.world_transform.anchor.y,
        node.world_transform.anchor.z,
        bbox.x0,
        bbox.y0,
        bbox.x1,
        bbox.y1,
        anchor_screen.x,
        anchor_screen.y
    );
}

void draw_diagnostic_nulls(Framebuffer& fb, const Scene& scene, const Camera2_5D& camera) {
    const f32 w = static_cast<f32>(fb.width());
    const f32 h = static_cast<f32>(fb.height());

    auto get_screen_pos = [&](const Vec3& world_pos) -> Vec2 {
        if (!camera.enabled) {
            return Vec2(world_pos.x, world_pos.y);
        }
        Mat4 view = camera.view_matrix();
        f32 focal = camera.zoom;
        if (camera.projection_mode == Camera2_5DProjectionMode::Fov) {
            focal = h / (2.0f * std::tan(glm::radians(camera.fov_deg) * 0.5f));
        }
        Vec2 screen;
        f32 depth;
        if (project_world_point_2_5d(camera, view, true, focal, world_pos, screen, depth)) {
            return Vec2(screen.x + w * 0.5f, screen.y + h * 0.5f);
        }
        return Vec2(world_pos.x, world_pos.y);
    };

    // Draw parent-child connections
    for (const auto& layer : scene.layers()) {
        if (layer.parent_name.empty()) continue;
        for (const auto& parent_layer : scene.layers()) {
            if (parent_layer.name == layer.parent_name) {
                Vec2 child_screen = get_screen_pos(layer.transform.position);
                Vec2 parent_screen = get_screen_pos(parent_layer.transform.position);
                renderer::bline(fb, parent_screen, child_screen, Color{1.0f, 0.5f, 0.0f, 0.7f});
                break;
            }
        }
    }

    // Draw null layer crosshairs and axes
    for (const auto& layer : scene.layers()) {
        if (layer.kind != LayerKind::Null) continue;
        Vec2 pos_screen = get_screen_pos(layer.transform.position);
        draw_crosshair(fb, pos_screen, 8.0f, Color{1.0f, 0.0f, 1.0f, 0.9f});

        Vec3 x_axis = layer.transform.position + layer.transform.rotation * Vec3(30.0f, 0.0f, 0.0f);
        Vec3 y_axis = layer.transform.position + layer.transform.rotation * Vec3(0.0f, 30.0f, 0.0f);
        Vec2 x_screen = get_screen_pos(x_axis);
        Vec2 y_screen = get_screen_pos(y_axis);

        renderer::bline(fb, pos_screen, x_screen, Color{1.0f, 0.0f, 0.0f, 0.9f});
        renderer::bline(fb, pos_screen, y_screen, Color{0.0f, 1.0f, 0.0f, 0.9f});
    }
}

} // namespace

SoftwareRenderer::SoftwareRenderer()
    : m_software_registry(std::make_unique<renderer::SoftwareRegistry>())
    , m_framebuffer_pool(std::make_shared<cache::FramebufferPool>())
    , m_executor(std::make_unique<graph::GraphExecutor>()) {
    renderer::register_builtin_processors(*m_software_registry);
}

SoftwareRenderer::~SoftwareRenderer() = default;

graph::GraphExecutor* SoftwareRenderer::executor() {
    return m_executor.get();
}

const graph::GraphExecutor* SoftwareRenderer::executor() const {
    return m_executor.get();
}

std::shared_ptr<Framebuffer> SoftwareRenderer::render_frame(const Composition& comp,
                                                            Frame frame) {
    profiling::ProfilingGuard scope(&m_counters, m_framebuffer_pool.get());

    auto res = graph::render_composition_frame(
        *this, m_node_cache, m_settings, m_registry, m_video_decoder.get(), comp, frame
    );
    return res;
}

std::shared_ptr<Framebuffer> SoftwareRenderer::render_scene(const Scene& scene,
                                                            const Camera& camera, i32 width,
                                                            i32 height) {
    profiling::ProfilingGuard scope(&m_counters, m_framebuffer_pool.get());

    auto res = graph::render_scene_via_graph(
        *this,
        m_node_cache,
        scene,
        camera,
        width,
        height,
        0,
        0.0f,
        m_settings,
        m_registry,
        m_video_decoder.get()
    );
    return res;
}

std::shared_ptr<Framebuffer> SoftwareRenderer::render_scene(
    const Scene& scene, const std::optional<Camera2_5D>& camera, i32 width, i32 height) {
    profiling::ProfilingGuard scope(&m_counters, m_framebuffer_pool.get());

    Scene effective_scene = scene;
    if (camera.has_value()) {
        effective_scene.set_camera_2_5d(*camera);
    }

    Camera default_camera;
    auto res = graph::render_scene_via_graph(
        *this,
        m_node_cache,
        effective_scene,
        default_camera,
        width,
        height,
        0,
        0.0f,
        m_settings,
        m_registry,
        m_video_decoder.get()
    );
    if (res && m_settings.diagnostics.enabled) {
        draw_diagnostic_nulls(*res, effective_scene, effective_scene.camera_2_5d());
    }
    return res;
}

std::string SoftwareRenderer::debug_render_graph(const Scene& scene, const Camera& camera,
                                                  i32 width, i32 height, Frame frame,
                                                  f32 frame_time) const {
    return graph::debug_scene_graph(
        const_cast<SoftwareRenderer&>(*this),
        m_node_cache,
        scene,
        camera,
        width,
        height,
        frame,
        frame_time,
        m_settings,
        m_registry,
        m_video_decoder.get()
    );
}

void SoftwareRenderer::apply_blur(Framebuffer& fb, f32 radius, const std::optional<raster::BBox>& clip) {
    const auto local_clip = to_local_clip(fb, clip);
    m_counters.blur_pixels.fetch_add(clipped_area(fb.width(), fb.height(), local_clip), std::memory_order_relaxed);
    CHRONON_ZONE_C("apply_blur", trace_category::kEffect);
    renderer::apply_blur(fb, radius, local_clip, 3);
}

void SoftwareRenderer::apply_effect_stack(Framebuffer& fb, const EffectStack& stack, float time_seconds, const std::optional<raster::BBox>& clip) {
    CHRONON_ZONE_C("apply_effect_stack", trace_category::kEffect);

    const auto local_clip = to_local_clip(fb, clip);
    const uint64_t area = clipped_area(fb.width(), fb.height(), local_clip);
    for (const auto& effect : stack) {
        if (effect.enabled && effect.effect_type == effects::EffectType::Blur) {
            m_counters.blur_pixels.fetch_add(area, std::memory_order_relaxed);
        }
    }

    renderer::apply_effect_stack(fb, stack, time_seconds, local_clip, m_settings.diagnostics.enabled);
}

void SoftwareRenderer::draw_node(Framebuffer& fb, const RenderNode& node,
                                 const RenderState& state, const Camera& camera, i32 width,
                                 i32 height) {
    CHRONON_ZONE_C("draw_node", trace_category::kRasterize);
    m_counters.pixels_touched.fetch_add(clipped_area(width, height, to_local_clip(fb, state.clip_rect)), std::memory_order_relaxed);
    auto* processor = software_registry().get_shape(node.shape.type);
    if (!processor) {
        SoftwareNodeDispatcher::draw_node(*this, fb, node, state, camera, width, height, software_registry());
        return;
    }
    processor->draw(*this, fb, node, state, camera, width, height);
    if (m_settings.diagnostics.enabled) {
        draw_layout_preview(fb, node, state, *processor);
    }
}

void SoftwareRenderer::composite_layer(Framebuffer& dst, const Framebuffer& src, BlendMode mode, const std::optional<raster::BBox>& clip) {
    m_counters.layers_rendered.fetch_add(1, std::memory_order_relaxed);
    CHRONON_ZONE_C("composite_layer", trace_category::kComposite);
    m_counters.pixels_touched.fetch_add(clipped_area(dst.width(), dst.height(), to_local_clip(dst, clip)), std::memory_order_relaxed);
    SoftwareCompositor::composite_layer(dst, src, mode, clip);
}

} // namespace chronon3d
