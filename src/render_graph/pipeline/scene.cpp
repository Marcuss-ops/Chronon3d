// ---------------------------------------------------------------------------
// scene.cpp — Thin orchestrator for render_scene_via_graph()
//
// The heavy lifting has been extracted into 5 focused phase files:
//   scene_frame_reuse.cpp    — Phase 1 (early-out fast-paths)
//   scene_dirty_phase.cpp    — Phase 2 (resolve + dirty rect)
//   scene_graph_phase.cpp    — Phase 3 (graph build/reuse + pool prealloc)
//   scene_execute_phase.cpp  — Phase 4 (tile/single-pass execution)
//   scene_commit_phase.cpp   — Phase 5 (save state for next frame)
//
// This file owns the helper utilities (diagnostic plan output) and the
// top-level orchestration flow.  The shared cross-phase data lives in
// SceneRenderContext (scene_phases.hpp).
// ---------------------------------------------------------------------------

#include "scene_phases.hpp"
#include "helpers.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <chronon3d/render_graph/preflight/preflight_render_graph.hpp>

#include <spdlog/spdlog.h>
#include <fmt/format.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace chronon3d::graph {

namespace {

// ── Output-path helpers ──────────────────────────────────────────────────────

std::string format_plan_output_path(std::string pattern, Frame frame) {
    const std::string replacement = fmt::format("{:04d}", static_cast<int64_t>(frame));
    const auto pos = pattern.find("####");
    if (pos != std::string::npos) {
        pattern.replace(pos, 4, replacement);
        return pattern;
    }

    const auto dot_pos = pattern.find_last_of('.');
    if (dot_pos != std::string::npos) {
        pattern.insert(dot_pos, "_" + replacement);
    } else {
        pattern += "_" + replacement;
    }
    return pattern;
}

bool write_plan_output_file(const std::string& path, const std::string& contents) {
    if (path.empty()) return true;

    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        if (ec) {
            spdlog::error("[graph-preflight] cannot create directory '{}': {}",
                p.parent_path().string(), ec.message());
            return false;
        }
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        spdlog::error("[graph-preflight] cannot open '{}' for writing", path);
        return false;
    }
    out << contents;
    if (!out) {
        spdlog::error("[graph-preflight] failed while writing '{}'", path);
        return false;
    }
    return true;
}

} // anonymous namespace

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
    float fps,
    std::string_view diagnostic_label
) {
    ZoneScoped;
    const auto t0 = std::chrono::steady_clock::now();
    const auto hits_before = node_cache.stats().hits;

    // ── Graph context + camera ────────────────────────────────────────────
    auto ctx = make_graph_context(
        backend, node_cache, camera, width, height, frame, frame_time,
        settings, registry, video_decoder, fps
    );

    ctx.light_context = scene.light_context();
    const auto resolved_camera = resolve_scene_camera(scene);
    if (resolved_camera.camera.enabled) {
        ctx.camera_2_5d = resolved_camera.camera;
        ctx.has_camera_2_5d = true;
        ctx.projection_ctx = renderer::make_projection_context(
            ctx.camera_2_5d, ctx.width, ctx.height);
        ctx.projection_ctx.ready = true;
    }

    // RAII guard: sets profiling thread-locals and restores on any exit path
    // (including early returns and exceptions).
    profiling::ProfilingGuard profiling_guard(ctx.counters, ctx.framebuffer_pool.get());

    SoftwareRenderer* sw_renderer = dynamic_cast<SoftwareRenderer*>(&backend);

    // ── Populate SceneRenderContext ───────────────────────────────────────
    detail::SceneRenderContext sctx;

    sctx.backend         = &backend;
    sctx.node_cache      = &node_cache;
    sctx.scene           = &scene;
    sctx.camera          = &camera;
    sctx.width           = width;
    sctx.height          = height;
    sctx.frame           = frame;
    sctx.frame_time      = frame_time;
    sctx.settings        = &settings;
    sctx.registry        = registry;
    sctx.video_decoder   = video_decoder;
    sctx.fps             = fps;
    sctx.diagnostic_label = diagnostic_label;

    sctx.ctx       = std::move(ctx);
    sctx.sw_renderer = sw_renderer;
    sctx.t0        = t0;

    // ── Phase 1: Frame reuse ──────────────────────────────────────────────
    // Try resolved scene reuse (combined fingerprint match) and static scene
    // fast-path (structure + camera + active_at unchanged, consecutive frame).
    if (detail::run_frame_reuse_phase(sctx) == detail::PhaseResult::Done) {
        const auto hits_after = node_cache.stats().hits;
        return sw_renderer->m_prev_framebuffer;
    }

    // ── Full path: set graph_structure_unchanged for later phases ──────
    sctx.ctx.graph_structure_unchanged =
        sctx.scene_structure_unchanged && !sctx.static_cam_changed && sctx.active_at_unchanged;

    // ── Diagnostic plan (preflight) ───────────────────────────────────────
    if (settings.diagnostic_plan) {
        // Temporarily disable counters during preflight (no need to accumulate
        // profiling data for a dry-run analysis pass).  RAII guard restores
        // both g_current_counters and g_current_framebuffer_pool on exit.
        profiling::ProfilingGuard diag_guard(
            nullptr, profiling::g_current_framebuffer_pool);
        auto report = debug_preflight_render_graph(
            backend, node_cache, scene, camera, width, height,
            frame, frame_time, settings, registry, video_decoder, fps);
        spdlog::info("[graph-preflight] label='{}' frame={} size={}x{}\n{}",
            diagnostic_label, static_cast<int>(frame), width, height, report.to_text());
        if (!settings.diagnostic_plan_output.empty()) {
            const auto report_path = format_plan_output_path(
                settings.diagnostic_plan_output, frame);
            if (write_plan_output_file(report_path, report.to_text())) {
                spdlog::info("[graph-preflight] report written to {}", report_path);
            }
        }
    }

    // ── Phase 2: Dirty analysis ───────────────────────────────────────────
    // Resolve layers, compute dirty rect, try fast_path_reuse for empty
    // dirty rect.
    if (detail::run_dirty_analysis_phase(sctx) == detail::PhaseResult::Done) {
        // fast_path_reuse case: state already updated inside the phase
        const auto hits_after = node_cache.stats().hits;
        return sw_renderer->m_prev_framebuffer;
    }

    // ── Phase 3: Graph build ─────────────────────────────────────────────
    // Wire up ping-pong + transform scratch, then build or reuse the
    // compiled render graph.  Pre-frame pool preallocation runs here.
    detail::run_graph_build_phase(sctx);

    // ── Phase 4: Execute ──────────────────────────────────────────────────
    // Tile-based (when safe and enabled) or traditional single-pass
    // graph execution.  Timing counters are recorded here.
    detail::run_execute_phase(sctx);

    // ── Phase 5: Commit ───────────────────────────────────────────────────
    // Save per-frame state: cached compiled graph, m_prev_framebuffer,
    // fingerprints, camera, layer bboxes.
    detail::run_commit_phase(sctx);

    const auto hits_after = node_cache.stats().hits;
    return sctx.fb_shared;
}

} // namespace chronon3d::graph
