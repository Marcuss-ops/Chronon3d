#include "utils/render_effects_processor.hpp"
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/render_graph/render_pipeline.hpp>
#include <chronon3d/render_graph/graph_executor.hpp>
#include <chronon3d/backends/software/software_compositor.hpp>
#include <chronon3d/backends/software/software_effect_runner.hpp>
#include <chronon3d/backends/software/software_node_dispatcher.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/software/builtin_processors.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <optional>
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>
#include "rasterizers/line_rasterizer.hpp"
#include "rasterizers/shape_rasterizer.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace chronon3d {
    struct RenderNode;
    struct RenderState;
    namespace raster { struct BBox; }
}

namespace chronon3d::renderer {

void apply_blur(Framebuffer& fb, f32 radius, const std::optional<raster::BBox>& clip);
void apply_color_effects(Framebuffer& fb, const LayerEffect& effect, const std::optional<raster::BBox>& clip);
void apply_effect_stack(Framebuffer& fb, const EffectStack& stack, float time_seconds, const std::optional<raster::BBox>& clip);
}

namespace chronon3d {

namespace {

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
    switch (type) {
    case ShapeType::None: return "None";
    case ShapeType::Rect: return "Rect";
    case ShapeType::RoundedRect: return "RoundedRect";
    case ShapeType::Circle: return "Circle";
    case ShapeType::Line: return "Line";
    case ShapeType::Path: return "Path";
    case ShapeType::Text: return "Text";
    case ShapeType::Image: return "Image";
    case ShapeType::Mesh: return "Mesh";
    case ShapeType::FakeBox3D: return "FakeBox3D";
    case ShapeType::GridPlane: return "GridPlane";
    case ShapeType::FakeExtrudedText: return "FakeExtrudedText";
    }
    return "Unknown";
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

} // namespace

SoftwareRenderer::SoftwareRenderer()
    : m_software_registry(std::make_unique<renderer::SoftwareRegistry>())
    , m_framebuffer_pool(std::make_shared<cache::FramebufferPool>())
    , m_executor(std::make_unique<graph::GraphExecutor>()) {
    renderer::register_builtin_processors(*m_software_registry);
}

SoftwareRenderer::~SoftwareRenderer() {
    if (profiling::g_current_trace == &m_trace) {
        profiling::g_current_trace = nullptr;
    }
}

graph::GraphExecutor* SoftwareRenderer::executor() {
    return m_executor.get();
}

const graph::GraphExecutor* SoftwareRenderer::executor() const {
    return m_executor.get();
}

std::shared_ptr<Framebuffer> SoftwareRenderer::render_frame(const Composition& comp,
                                                            Frame frame) {
    profiling::g_current_trace = &m_trace;
    profiling::g_current_frame = static_cast<int32_t>(frame);
    profiling::g_current_counters = &m_counters;
    profiling::g_current_framebuffer_pool = m_framebuffer_pool.get();
    TraceScope scope(&m_trace, "render_frame", "frame", static_cast<int32_t>(frame));

    auto res = graph::render_composition_frame(
        *this, m_node_cache, m_settings, m_registry, m_video_decoder.get(), comp, frame
    );

    profiling::g_current_counters = nullptr;
    profiling::g_current_framebuffer_pool = nullptr;
    return res;
}

std::shared_ptr<Framebuffer> SoftwareRenderer::render_scene(const Scene& scene,
                                                            const Camera& camera, i32 width,
                                                            i32 height) {
    profiling::g_current_trace = &m_trace;
    profiling::g_current_frame = 0;
    profiling::g_current_counters = &m_counters;
    profiling::g_current_framebuffer_pool = m_framebuffer_pool.get();
    TraceScope scope(&m_trace, "render_scene", "frame", 0);

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

    profiling::g_current_counters = nullptr;
    profiling::g_current_framebuffer_pool = nullptr;
    return res;
}

std::shared_ptr<Framebuffer> SoftwareRenderer::render_scene(
    const Scene& scene, const std::optional<Camera2_5D>& camera, i32 width, i32 height) {
    profiling::g_current_trace = &m_trace;
    profiling::g_current_frame = 0;
    profiling::g_current_counters = &m_counters;
    profiling::g_current_framebuffer_pool = m_framebuffer_pool.get();
    TraceScope scope(&m_trace, "render_scene_2_5d", "frame", 0);

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

    profiling::g_current_counters = nullptr;
    profiling::g_current_framebuffer_pool = nullptr;
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
    m_counters.blur_pixels.fetch_add(static_cast<uint64_t>(fb.width() * fb.height()), std::memory_order_relaxed);
    CHRONON_ZONE_C("apply_blur", trace_category::kEffect);
    renderer::apply_blur(fb, radius, clip);
}

void SoftwareRenderer::apply_effect_stack(Framebuffer& fb, const EffectStack& stack, float time_seconds, const std::optional<raster::BBox>& clip) {
    CHRONON_ZONE_C("apply_effect_stack", trace_category::kEffect);
    
    // Count blur pixels if any blur effect is present in the stack
    for (const auto& effect : stack) {
        if (effect.enabled && effect.params.type() == typeid(BlurParams)) {
            m_counters.blur_pixels.fetch_add(static_cast<uint64_t>(fb.width() * fb.height()), std::memory_order_relaxed);
        }
    }

    renderer::apply_effect_stack(fb, stack, time_seconds, clip);
}

void SoftwareRenderer::draw_node(Framebuffer& fb, const RenderNode& node,
                                 const RenderState& state, const Camera& camera, i32 width,
                                 i32 height) {
    CHRONON_ZONE_C("draw_node", trace_category::kRasterize);
    uint64_t touched = static_cast<uint64_t>(std::max(0, width)) * static_cast<uint64_t>(std::max(0, height));
    if (state.clip_rect) {
        raster::BBox local_clip = *state.clip_rect;
        local_clip.x0 -= fb.origin_x();
        local_clip.x1 -= fb.origin_x();
        local_clip.y0 -= fb.origin_y();
        local_clip.y1 -= fb.origin_y();
        local_clip.clip_to(width, height);
        touched = local_clip.is_empty()
            ? 0
            : static_cast<uint64_t>(std::max(0, local_clip.x1 - local_clip.x0)) *
              static_cast<uint64_t>(std::max(0, local_clip.y1 - local_clip.y0));
    }
    m_counters.pixels_touched.fetch_add(touched, std::memory_order_relaxed);
    auto* processor = software_registry().get_shape(node.shape.type);
    if (!processor) {
        SoftwareNodeDispatcher::draw_node(*this, fb, node, state, camera, width, height, software_registry());
        return;
    }
    processor->draw(*this, fb, node, state, camera, width, height);
    if (m_settings.diagnostic) {
        draw_layout_preview(fb, node, state, *processor);
    }
}

void SoftwareRenderer::composite_layer(Framebuffer& dst, const Framebuffer& src, BlendMode mode, const std::optional<raster::BBox>& clip) {
    m_counters.layers_rendered.fetch_add(1, std::memory_order_relaxed);
    CHRONON_ZONE_C("composite_layer", trace_category::kComposite);
    uint64_t touched = static_cast<uint64_t>(dst.width()) * static_cast<uint64_t>(dst.height());
    if (clip) {
        raster::BBox local_clip = *clip;
        local_clip.x0 -= dst.origin_x();
        local_clip.x1 -= dst.origin_x();
        local_clip.y0 -= dst.origin_y();
        local_clip.y1 -= dst.origin_y();
        local_clip.clip_to(dst.width(), dst.height());
        touched = local_clip.is_empty()
            ? 0
            : static_cast<uint64_t>(std::max(0, local_clip.x1 - local_clip.x0)) *
              static_cast<uint64_t>(std::max(0, local_clip.y1 - local_clip.y0));
    }
    m_counters.pixels_touched.fetch_add(touched, std::memory_order_relaxed);
    SoftwareCompositor::composite_layer(dst, src, mode, clip);
}

} // namespace chronon3d
