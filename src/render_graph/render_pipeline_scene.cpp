#include <chronon3d/render_graph/render_pipeline.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <chronon3d/render_graph/graph_builder.hpp>
#include <chronon3d/render_graph/graph_executor.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/math/projector_2_5d.hpp>
#include "builder/graph_builder_pipeline.hpp"
#include "builder/graph_builder_internal.hpp"
#include "render_pipeline_helpers.hpp"
#include "render_pipeline_scene_internal.hpp"
#include <spdlog/spdlog.h>
#include <chrono>
#include <iostream>

namespace chronon3d::graph {

std::shared_ptr<Framebuffer> render_scene_via_graph(
    RenderBackend& backend,
    cache::NodeCache& node_cache,
    const Scene& scene,
    const Camera& camera,
    i32 width,
    i32 height,
    Frame frame,
    f32 frame_time,
    const RenderSettings& settings,
    const CompositionRegistry* registry,
    video::VideoFrameDecoder* video_decoder,
    float fps
) {
    ZoneScoped;
    const auto t0 = std::chrono::steady_clock::now();
    const auto hits_before = node_cache.stats().hits;

    const auto t_build0 = std::chrono::steady_clock::now();
    auto ctx = make_graph_context(
        backend, node_cache, camera, width, height, frame, frame_time,
        settings, registry, video_decoder, fps
    );

    profiling::g_current_trace = ctx.trace;
    profiling::g_current_frame = static_cast<int32_t>(frame);
    profiling::g_current_counters = ctx.counters;

    ctx.light_context = scene.light_context();
    if (scene.camera_2_5d().enabled) {
        ctx.camera_2_5d = scene.camera_2_5d();
        ctx.has_camera_2_5d = true;
        ctx.projection_ctx = renderer::make_projection_context(
            ctx.camera_2_5d, ctx.width, ctx.height);
        ctx.projection_ctx.ready = true;
    }

    SoftwareRenderer* sw_renderer = dynamic_cast<SoftwareRenderer*>(&backend);

    std::optional<uint64_t> current_scene_fingerprint;
    auto ensure_scene_fingerprint = [&]() -> uint64_t {
        if (!current_scene_fingerprint.has_value()) {
            if (sw_renderer) {
                current_scene_fingerprint = sw_renderer->m_scene_hasher.compute_fingerprint(scene, frame);
            } else {
                current_scene_fingerprint = compute_scene_fingerprint(scene, frame);
            }
        }
        return *current_scene_fingerprint;
    };

    // ── Quick skip: consecutive frame with no changes ───────────────────
    if (sw_renderer &&
        settings.enable_dirty_rects &&
        sw_renderer->m_prev_framebuffer &&
        sw_renderer->m_prev_framebuffer->width() == width &&
        sw_renderer->m_prev_framebuffer->height() == height &&
        sw_renderer->m_prev_frame == frame - 1)
    {
        CHRONON_ZONE_C("dirty_fast_check", trace_category::kFrame);
        const Camera2_5D& cam = ctx.camera_2_5d;
        const bool cam_changed = detail::camera_changed(
            cam, &sw_renderer->m_prev_camera, sw_renderer->m_prev_camera_valid);

        const uint64_t fingerprint = ensure_scene_fingerprint();
        if (!cam_changed && sw_renderer->m_prev_scene_fingerprint == fingerprint) {
            sw_renderer->m_last_dirty_area_ratio = 0.0;
            sw_renderer->m_prev_frame = frame;
            sw_renderer->m_prev_scene_fingerprint = fingerprint;
            sw_renderer->m_prev_camera = cam;
            sw_renderer->m_prev_camera_valid = cam.enabled;
            if (ctx.counters) {
                ctx.counters->dirty_union_area_pixels.store(0, std::memory_order_relaxed);
            }
            telemetry::record_render_telemetry(make_telemetry_row(
                "scene_render", frame, width, height,
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(),
                0.0, 0.0, 0.0, 0.0, 1,
                static_cast<int>(scene.layers().size()), ctx.counters
            ));
            profiling::g_current_counters = nullptr;
            return sw_renderer->m_prev_framebuffer;
        }
    }

    // ── Full path: resolve, dirty rect, graph, execute ──────────────────
    const auto resolved = detail::resolve_layers(scene, ctx);

    auto dirty_out = detail::compute_dirty_rect(
        ctx, resolved, settings, sw_renderer, frame, width, height);

    // ── Dirty ratio / counters / diagnostics ────────────────────────────
    double dirty_ratio = 1.0;
    u64 dirty_union_area_pixels = 0;
    if (dirty_out.dirty_rect) {
        const int dw = std::max(0, dirty_out.dirty_rect->x1 - dirty_out.dirty_rect->x0);
        const int dh = std::max(0, dirty_out.dirty_rect->y1 - dirty_out.dirty_rect->y0);
        const double area = static_cast<double>(dw) * static_cast<double>(dh);
        const double total = static_cast<double>(width) * height;
        if (total > 0) dirty_ratio = area / total;
        dirty_union_area_pixels = static_cast<u64>(area);
    }
    if (ctx.counters) {
        ctx.counters->dirty_union_area_pixels.store(dirty_union_area_pixels, std::memory_order_relaxed);
    }
    if (sw_renderer) {
        sw_renderer->m_last_dirty_area_ratio = dirty_ratio;
    }
    ctx.dirty_rect = dirty_out.dirty_rect;
    ctx.reuse_prev_framebuffer = dirty_out.use_dirty_rects;

    if (sw_renderer && ctx.diagnostics_enabled) {
        if (dirty_out.dirty_rect) {
            spdlog::info(
                "[dirty-debug] frame={} use_dirty_rects={} prev_fb={} dirty_rect=[{},{} -> {},{}] prev_frame={}",
                static_cast<int>(frame),
                dirty_out.use_dirty_rects ? 1 : 0,
                sw_renderer->m_prev_framebuffer ? 1 : 0,
                dirty_out.dirty_rect->x0, dirty_out.dirty_rect->y0,
                dirty_out.dirty_rect->x1, dirty_out.dirty_rect->y1,
                static_cast<int>(sw_renderer->m_prev_frame));
        } else {
            spdlog::info(
                "[dirty-debug] frame={} use_dirty_rects={} prev_fb={} dirty_rect=null prev_frame={}",
                static_cast<int>(frame),
                dirty_out.use_dirty_rects ? 1 : 0,
                sw_renderer->m_prev_framebuffer ? 1 : 0,
                static_cast<int>(sw_renderer->m_prev_frame));
        }
    }

    // ── Fast-path reuse: empty dirty rect ───────────────────────────────
    const bool fast_path_reuse = sw_renderer &&
                                 settings.enable_dirty_rects &&
                                 dirty_out.dirty_rect &&
                                 dirty_out.dirty_rect->is_empty() &&
                                 sw_renderer->m_prev_framebuffer &&
                                 sw_renderer->m_prev_framebuffer->width() == width &&
                                 sw_renderer->m_prev_framebuffer->height() == height;

    const auto t_build1 = std::chrono::steady_clock::now();

    if (fast_path_reuse) {
        if (ctx.diagnostics_enabled) {
            spdlog::info("[dirty-debug] frame={} fast_path_reuse=1", static_cast<int>(frame));
        }
        sw_renderer->m_last_dirty_area_ratio = 0.0;
        sw_renderer->m_prev_layer_bboxes = std::move(dirty_out.layer_bboxes);
        sw_renderer->m_prev_frame = frame;
        sw_renderer->m_prev_scene_fingerprint = ensure_scene_fingerprint();
        sw_renderer->m_prev_camera = resolved.camera.camera;
        sw_renderer->m_prev_camera_valid = resolved.camera.camera.enabled;
        telemetry::record_render_telemetry(make_telemetry_row(
            "scene_render", frame, width, height,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(),
            std::chrono::duration<double, std::milli>(t_build1 - t_build0).count(),
            0.0, 0.0, 0.0, 1,
            static_cast<int>(scene.layers().size()), ctx.counters
        ));
        profiling::g_current_counters = nullptr;
        return sw_renderer->m_prev_framebuffer;
    }

    // ── Build + execute render graph ────────────────────────────────────
    RenderGraph graph = [&]() {
        CHRONON_ZONE_C("build_graph", trace_category::kGraph);
        return detail::build_graph(scene, ctx, resolved);
    }();
    const auto t_build2 = std::chrono::steady_clock::now();

    const auto t_exec0 = std::chrono::steady_clock::now();
    std::shared_ptr<Framebuffer> fb_shared;
    {
        CHRONON_ZONE_C("graph_execute", trace_category::kGraph);
        if (sw_renderer && sw_renderer->executor()) {
            fb_shared = sw_renderer->executor()->execute(graph, graph.output(), ctx);
        } else {
            GraphExecutor local_executor;
            fb_shared = local_executor.execute(graph, graph.output(), ctx);
        }
    }
    const auto t_exec1 = std::chrono::steady_clock::now();

    // ── Save state for next frame ───────────────────────────────────────
    if (sw_renderer) {
        sw_renderer->m_prev_framebuffer = fb_shared;
        sw_renderer->m_prev_layer_bboxes = std::move(dirty_out.layer_bboxes);
        sw_renderer->m_prev_frame = frame;
        sw_renderer->m_prev_scene_fingerprint = ensure_scene_fingerprint();
        sw_renderer->m_prev_camera = resolved.camera.camera;
        sw_renderer->m_prev_camera_valid = resolved.camera.camera.enabled;
    }
    const auto hits_after = node_cache.stats().hits;

    const auto t1 = std::chrono::steady_clock::now();
    telemetry::record_render_telemetry(make_telemetry_row(
        "scene_render", frame, width, height,
        std::chrono::duration<double, std::milli>(t1 - t0).count(),
        std::chrono::duration<double, std::milli>(t_build2 - t_build0).count(),
        std::chrono::duration<double, std::milli>(t_exec1 - t_exec0).count(),
        0.0, 0.0,
        hits_after > hits_before ? 1 : 0,
        static_cast<int>(scene.layers().size()), ctx.counters
    ));

    profiling::g_current_counters = nullptr;
    return fb_shared;
}

} // namespace chronon3d::graph
