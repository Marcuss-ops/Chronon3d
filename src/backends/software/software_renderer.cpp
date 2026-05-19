#include "utils/render_effects_processor.hpp"
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/render_graph/render_pipeline.hpp>
#include <chronon3d/backends/software/software_compositor.hpp>
#include <chronon3d/backends/software/software_effect_runner.hpp>
#include <chronon3d/backends/software/software_node_dispatcher.hpp>
#include <chronon3d/backends/software/builtin_processors.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/core/profiling.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d {

SoftwareRenderer::SoftwareRenderer()
    : m_software_registry(std::make_unique<renderer::SoftwareRegistry>()) {
    renderer::register_builtin_processors(*m_software_registry);
}

SoftwareRenderer::~SoftwareRenderer() {
    if (profiling::g_current_trace == &m_trace) {
        profiling::g_current_trace = nullptr;
    }
}

std::unique_ptr<Framebuffer> SoftwareRenderer::render_frame(const Composition& comp,
                                                            Frame frame) {
    profiling::g_current_trace = &m_trace;
    profiling::g_current_frame = static_cast<int32_t>(frame);
    TraceScope scope(&m_trace, "render_frame", "frame", static_cast<int32_t>(frame));

    return graph::render_composition_frame(
        *this, m_node_cache, m_settings, m_registry, m_video_decoder.get(), comp, frame
    );
}

std::shared_ptr<Framebuffer> SoftwareRenderer::render_scene(const Scene& scene,
                                                            const Camera& camera, i32 width,
                                                            i32 height) {
    profiling::g_current_trace = &m_trace;
    profiling::g_current_frame = 0;
    TraceScope scope(&m_trace, "render_scene", "frame", 0);

    return graph::render_scene_via_graph(
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
}

std::shared_ptr<Framebuffer> SoftwareRenderer::render_scene(
    const Scene& scene, const std::optional<Camera2_5D>& camera, i32 width, i32 height) {
    profiling::g_current_trace = &m_trace;
    profiling::g_current_frame = 0;
    TraceScope scope(&m_trace, "render_scene_2_5d", "frame", 0);

    Scene effective_scene = scene;
    if (camera.has_value()) {
        effective_scene.set_camera_2_5d(*camera);
    }

    Camera default_camera;
    return graph::render_scene_via_graph(
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

void SoftwareRenderer::apply_blur(Framebuffer& fb, f32 radius) {
    m_counters.blur_pixels.fetch_add(static_cast<uint64_t>(fb.width() * fb.height()), std::memory_order_relaxed);
    CHRONON_ZONE_C("apply_blur", trace_category::kEffect);
    SoftwareEffectRunner::apply_blur(fb, radius);
}

void SoftwareRenderer::draw_node(Framebuffer& fb, const RenderNode& node,
                                 const RenderState& state, const Camera& camera, i32 width,
                                 i32 height) {
    m_counters.pixels_touched.fetch_add(static_cast<uint64_t>(width * height), std::memory_order_relaxed);
    CHRONON_ZONE_C("draw_node", trace_category::kRasterize);
    SoftwareNodeDispatcher::draw_node(*this, fb, node, state, camera, width, height, software_registry());
}

void SoftwareRenderer::apply_effect_stack(Framebuffer& fb, const EffectStack& stack, float time_seconds) {
    CHRONON_ZONE_C("apply_effect_stack", trace_category::kEffect);
    
    // Count blur pixels if any blur effect is present in the stack
    for (const auto& effect : stack) {
        if (effect.enabled && effect.params.type() == typeid(BlurParams)) {
            m_counters.blur_pixels.fetch_add(static_cast<uint64_t>(fb.width() * fb.height()), std::memory_order_relaxed);
        }
    }

    SoftwareEffectRunner::apply_effect_stack(fb, stack, software_registry(), time_seconds);
}

void SoftwareRenderer::composite_layer(Framebuffer& dst, const Framebuffer& src, BlendMode mode) {
    m_counters.pixels_touched.fetch_add(static_cast<uint64_t>(dst.width() * dst.height()), std::memory_order_relaxed);
    m_counters.layers_rendered.fetch_add(1, std::memory_order_relaxed);
    CHRONON_ZONE_C("composite_layer", trace_category::kComposite);
    SoftwareCompositor::composite_layer(dst, src, mode);
}
} // namespace chronon3d
