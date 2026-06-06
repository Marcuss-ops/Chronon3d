#include <chronon3d/render_graph/render_pipeline.hpp>
#include <chronon3d/render_graph/preflight_render_graph.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <chronon3d/render_graph/graph_builder.hpp>
#include <chronon3d/render_graph/graph_executor.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/math/projector_2_5d.hpp>
#include "../builder/graph_builder_pipeline.hpp"
#include "../builder/graph_builder_coordinates.hpp"
#include "../builder/graph_builder_internal.hpp"
#include <chronon3d/render_graph/builder/graph_build_pipeline.hpp>
#include <chronon3d/render_graph/optimizer/graph_optimizer.hpp>
#include <chronon3d/render_graph/compiler/frame_graph_compiler.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/core/tile_grid.hpp>
#include <chronon3d/core/dirty_tile_mask.hpp>
#include "helpers.hpp"
#include "scene_internal.hpp"
#include "scene_refresh.hpp"
#include "scene_tile_execution.hpp"
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace chronon3d::graph {

namespace {

inline uint64_t to_ms_u64(double ms) {
    return static_cast<uint64_t>(std::llround(std::max(0.0, ms)));
}

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
            spdlog::error("[graph-preflight] cannot create directory '{}': {}", p.parent_path().string(), ec.message());
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

    const auto t_build0 = std::chrono::steady_clock::now();
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

    // ── Resolved scene reuse: consecutive/same frame ─────────────────────
    // If the already-evaluated scene produces identical visual output as the
    // previous frame, skip resolve_layers(), dirty computation, graph build,
    // graph execution and compositing entirely.
    //
    // CRITICAL: We require BOTH static_fp AND active_at_fp to match.
    // - static_fp confirms layer structure is unchanged
    // - active_at_fp confirms which layers are active at each frame is unchanged
    //
    // Using only static_fp would incorrectly reuse frame 0's output for DarkGridBackground
    // frame 1 (active_at changes from true→false, but static_fp matches since structure
    // is the same — yet frame 1's evaluated scene is empty, visually different).
    //
    // This block must come BEFORE resolve_layers() and compute_dirty_rect() so
    // that identical consecutive frames avoid even entering the dirty system.
    if (sw_renderer &&
        sw_renderer->m_prev_framebuffer &&
        sw_renderer->m_prev_framebuffer->width() == width &&
        sw_renderer->m_prev_framebuffer->height() == height &&
        (sw_renderer->m_prev_frame == frame - 1 || sw_renderer->m_prev_frame == frame))
    {
        CHRONON_ZONE_C("resolved_scene_reuse", trace_category::kFrame);
        const Camera2_5D& cam = ctx.camera_2_5d;
        const bool cam_changed = detail::camera_changed(
            cam, &sw_renderer->m_prev_camera, sw_renderer->m_prev_camera_valid);
        const uint64_t static_fp = sw_renderer->m_scene_hasher.compute_static_fingerprint(scene);
        const uint64_t active_at_fp = sw_renderer->m_scene_hasher.compute_active_at_fingerprint(scene, frame);
        const uint64_t combined_fp = static_fp ^ (active_at_fp * 0x9e3779b97f4a7c15ULL);

        if (!cam_changed && sw_renderer->m_prev_scene_fingerprint == combined_fp) {
            sw_renderer->m_last_dirty_area_ratio = 0.0;
            sw_renderer->m_prev_frame = frame;
            sw_renderer->m_prev_scene_fingerprint = combined_fp;
            sw_renderer->m_prev_camera = cam;
            sw_renderer->m_prev_camera_valid = cam.enabled;
            if (ctx.counters) {
                ctx.counters->dirty_union_area_pixels.store(0, std::memory_order_relaxed);
                ctx.counters->clear_skipped_calls.fetch_add(1, std::memory_order_relaxed);
                ctx.counters->clear_skipped_pixels.fetch_add(
                    static_cast<uint64_t>(width) * height,
                    std::memory_order_relaxed
                );
            }
            if (ctx.diagnostics_enabled) {
                spdlog::info("[resolved-reuse] frame={} combined_fingerprint_match=1", static_cast<int>(frame));
            }
            return sw_renderer->m_prev_framebuffer;
        }
    }

    // ── Fingerprints (pre-computed for fast-paths + executor hint) ──
    // Compute once and reuse: the content-sensitive static fast-path, the
    // structure-only graph reuse path, the dirty-rect fast-path, and the
    // execution plan cache hint all need to know whether the scene structure
    // and camera are unchanged since the previous frame.
    bool scene_structure_unchanged = false;
    bool static_cam_changed = true;
    bool scene_is_static = false;
    uint64_t current_static_fp = 0;
    uint64_t current_active_at_fp = 0;
    uint64_t current_structure_fp = 0;
    if (sw_renderer && sw_renderer->m_prev_static_scene_fingerprint != 0) {
        current_static_fp = sw_renderer->m_scene_hasher.compute_static_fingerprint(scene);
        current_structure_fp = sw_renderer->m_scene_hasher.compute_structure_fingerprint(scene);
        scene_structure_unchanged = (current_structure_fp == sw_renderer->m_prev_graph_structure_fingerprint);
        const Camera2_5D& cam = ctx.camera_2_5d;
        static_cam_changed = detail::camera_changed(
            cam, &sw_renderer->m_prev_camera, sw_renderer->m_prev_camera_valid);
        scene_is_static = sw_renderer->m_scene_hasher.is_static_scene(scene);
        current_active_at_fp = sw_renderer->m_scene_hasher.compute_active_at_fingerprint(scene, frame);
    }

    // ── Static scene fast-path (no dirty rects required) ──────────────
    // When the scene is unchanged and the camera is the same, skip graph
    // building + execution entirely — return the previous framebuffer.
    //
    // For truly static scenes (no AnimatedTransform, no Video layers, no
    // transitions, no time-dependent expressions), we also allow consecutive
    // frame reuse (m_prev_frame == frame - 1).  This enables the fast-path
    // to work in real video exports (frames 0,1,2,3...) not just benchmarks
    // where frame is always 0.  For frame-dependent scenes (transitions,
    // animations), we require exact frame match for safety.
    //
    // CRITICAL: The active_at_fingerprint check is required because
    // compute_static_fingerprint() hashes ALL layers regardless of active_at(frame).
    // Scenes where layers activate/deactivate across frames (e.g. DarkGridBackground
    // with duration=1) would otherwise incorrectly match — returning stale output.
    const bool frame_reuse = (sw_renderer->m_prev_frame == frame) ||
        (scene_is_static && sw_renderer->m_prev_frame == frame - 1);
    const bool active_at_unchanged = (current_active_at_fp != 0) &&
        (current_active_at_fp == sw_renderer->m_prev_active_at_fingerprint);

    if (sw_renderer &&
        sw_renderer->m_prev_framebuffer &&
        sw_renderer->m_prev_framebuffer->width() == width &&
        sw_renderer->m_prev_framebuffer->height() == height &&
        frame_reuse &&
        scene_structure_unchanged && !static_cam_changed && active_at_unchanged &&
        sw_renderer->m_prev_static_scene_fingerprint != 0 &&
        current_static_fp == sw_renderer->m_prev_static_scene_fingerprint)
    {
        CHRONON_ZONE_C("static_scene_fast_check", trace_category::kFrame);
        sw_renderer->m_last_dirty_area_ratio = 0.0;
        sw_renderer->m_prev_frame = frame;
        sw_renderer->m_prev_camera = ctx.camera_2_5d;
        sw_renderer->m_prev_camera_valid = ctx.camera_2_5d.enabled;
        if (ctx.counters) {
            ctx.counters->dirty_union_area_pixels.store(0, std::memory_order_relaxed);
            ctx.counters->clear_skipped_calls.fetch_add(1, std::memory_order_relaxed);
            ctx.counters->clear_skipped_pixels.fetch_add(
                static_cast<uint64_t>(width) * height,
                std::memory_order_relaxed
            );
        }
        if (ctx.diagnostics_enabled) {
            spdlog::info("[static-fastpath] frame={} static_fingerprint_match=1",
                static_cast<int>(frame));
        }
        return sw_renderer->m_prev_framebuffer;
    }

    // ── Full path: resolve, dirty rect, graph, execute ──────────────────
    // If the scene structure and camera are unchanged, inform the executor
    // so it can skip compute_structure_signature() and reuse the cached
    // execution plan (topological sort + consumer counts) directly.
    ctx.graph_structure_unchanged = scene_structure_unchanged && !static_cam_changed && active_at_unchanged;

    if (settings.diagnostic_plan) {
        // Temporarily disable counters during preflight (no need to
        // accumulate profiling data for a dry-run analysis pass).
        // RAII guard restores both g_current_counters and
        // g_current_framebuffer_pool on any exit path.
        profiling::ProfilingGuard diag_guard(nullptr, profiling::g_current_framebuffer_pool);
        auto report = debug_preflight_render_graph(
            backend,
            node_cache,
            scene,
            camera,
            width,
            height,
            frame,
            frame_time,
            settings,
            registry,
            video_decoder,
            fps
        );
        spdlog::info("[graph-preflight] label='{}' frame={} size={}x{}\n{}",
                     diagnostic_label, static_cast<int>(frame), width, height, report.to_text());
        if (!settings.diagnostic_plan_output.empty()) {
            const auto report_path = format_plan_output_path(settings.diagnostic_plan_output, frame);
            if (write_plan_output_file(report_path, report.to_text())) {
                spdlog::info("[graph-preflight] report written to {}", report_path);
            }
        }
    }

    const auto t_resolve0 = std::chrono::steady_clock::now();
    const auto resolved = detail::resolve_layers(scene, ctx);
    const auto t_resolve1 = std::chrono::steady_clock::now();

    const auto t_dirty0 = std::chrono::steady_clock::now();
    auto dirty_out = detail::compute_dirty_rect(
        ctx, resolved, scene, settings, sw_renderer, frame, width, height);
    const auto t_dirty1 = std::chrono::steady_clock::now();

    // ── DEBUG: CHRONON_DEBUG_DISABLE_DIRTY ────────────────────────────
    // Override ambiente per disabilitare dirty rects e forzare full-frame render.
    // Utile per isolare bug di clipping legati alla dirty rect.
    if (std::getenv("CHRONON_DEBUG_DISABLE_DIRTY")) {
        dirty_out.dirty_rect = std::nullopt;
        dirty_out.use_dirty_rects = false;
        dirty_out.use_dirty_tiles = false;
        ctx.dirty_rect = std::nullopt;
        ctx.reuse_prev_framebuffer = false;
        spdlog::warn("[VDBG] CHRONON_DEBUG_DISABLE_DIRTY attivo — dirty rects disabilitati");
    }

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
        // Use combined_fp for consistency with resolved-reuse block and full-path save
        const uint64_t fp_static = sw_renderer->m_scene_hasher.compute_static_fingerprint(scene);
        const uint64_t fp_active_at = sw_renderer->m_scene_hasher.compute_active_at_fingerprint(scene, frame);
        const uint64_t fp_combined = fp_static ^ (fp_active_at * 0x9e3779b97f4a7c15ULL);
        sw_renderer->m_prev_scene_fingerprint = fp_combined;
        sw_renderer->m_prev_static_scene_fingerprint = fp_static;
        sw_renderer->m_prev_active_at_fingerprint = fp_active_at;
        sw_renderer->m_prev_camera = resolved.camera.camera;
        sw_renderer->m_prev_camera_valid = resolved.camera.camera.enabled;
        return sw_renderer->m_prev_framebuffer;
    }

    // ── Build (or reuse cached) render graph ─────────────────────────────
    // Phase 1: when graph_structure_unchanged is true, the graph topology is
    // identical to the previous frame — reuse the cached graph instead of
    // rebuilding and re-optimizing from scratch.
    const bool can_reuse_compiled_graph = ctx.graph_structure_unchanged &&
                                          sw_renderer &&
                                          sw_renderer->m_cached_compiled_graph != nullptr &&
                                          sw_renderer->m_cached_compiled_width == width &&
                                          sw_renderer->m_cached_compiled_height == height;

    CompiledFrameGraph compiled;

    const auto t_graph0 = std::chrono::steady_clock::now();
    bool graph_reused = false;
    if (can_reuse_compiled_graph) {
        // Reuse the cached compiled graph from the previous frame
        if (ctx.counters) {
            ctx.counters->graph_cache_hits.fetch_add(1, std::memory_order_relaxed);
        }
        compiled = std::move(*sw_renderer->m_cached_compiled_graph);
        sw_renderer->m_cached_compiled_graph.reset();

        const auto t_refresh0 = std::chrono::steady_clock::now();
        detail::refresh_compiled_graph_payloads(compiled, scene, ctx, resolved);
        const auto t_refresh1 = std::chrono::steady_clock::now();
        if (ctx.counters) {
            ctx.counters->compiled_graph_refresh_ms.fetch_add(
                to_ms_u64(std::chrono::duration<double, std::milli>(t_refresh1 - t_refresh0).count()),
                std::memory_order_relaxed);
        }
        compiled.skip_initial_clear = false;
        compiled.early_exit_skip.assign(compiled.graph.size(), false);

        ctx.skip_initial_clear = compiled.skip_initial_clear;
        ctx.early_exit_skip = compiled.early_exit_skip;

        if (ctx.diagnostics_enabled) {
            spdlog::info("[graph-cache] frame={} reusing cached compiled graph ({} live nodes)",
                         static_cast<int>(frame), compiled.graph.live_count());
        }
        graph_reused = true;
    } else {
        if (ctx.counters) {
            ctx.counters->graph_cache_misses.fetch_add(1, std::memory_order_relaxed);
        }
        RenderGraph graph;
        // Full build path — construct the render graph via GraphBuildPipeline.
        // Uses build_with_resolved() to avoid a redundant resolve_layers() call
        // since we already resolved above for dirty-rect computation.
        {
            CHRONON_ZONE_C("build_graph", trace_category::kGraph);
            auto mutable_ctx = ctx;
            GraphBuildPipeline pipeline;
            pipeline.add_default_passes();
            GraphBuildContext::ResolvedData pre_resolved;
            pre_resolved.layers = resolved.layers;
            pre_resolved.camera = resolved.camera;
            graph = pipeline.build_with_resolved(scene, mutable_ctx, pre_resolved);
            ctx.skip_initial_clear = mutable_ctx.skip_initial_clear;
            ctx.early_exit_skip = std::move(mutable_ctx.early_exit_skip);
        }

        if (ctx.diagnostics_enabled) {
            for (size_t i = 0; i < graph.size(); ++i) {
                if (!graph.has_node(i)) continue;
                std::string inputs_str;
                for (auto in : graph.inputs(i)) {
                    inputs_str += std::to_string(in) + " (" + graph.node(in).name() + "), ";
                }
                spdlog::info("[graph-structure] node_id={} name='{}' inputs=[{}]", i, graph.node(i).name(), inputs_str);
            }
        }

        // Compile path (which includes optimization)
        FrameGraphCompiler compiler;
        FrameGraphCompileOptions compile_options;
        compile_options.run_optimizer = true;
        compile_options.compute_lifetimes = true;
        compile_options.compute_bboxes = settings.diagnostic_plan;
        compile_options.include_diagnostics = settings.diagnostic_plan;

        compiled = compiler.compile(std::move(graph), ctx, compile_options);
    }
    const auto t_graph1 = std::chrono::steady_clock::now();
    const auto t_build2 = std::chrono::steady_clock::now();

    // ── Execute: tile-based (V1) or traditional single-pass ─────────────
    const auto t_exec0 = std::chrono::steady_clock::now();
    std::shared_ptr<Framebuffer> fb_shared;

    // Tile execution is incompatible with spatial effects (glow, bloom, drop shadow,
    // blur, distort, temporal).  When tile execution re-executes the full graph per
    // tile, the effect stack runs with a tile-scoped clip_rect.  The blur kernel in
    // these effects reads pixels outside the tile boundary — those pixels are zero
    // or garbage (from the fresh per-tile framebuffer), producing visible seams at
    // tile edges.  Disable tile execution when ANY layer has spatial effects.
    const bool tile_safe = !detail::has_layer_with_spatial_effects(resolved, frame);

    // ── Dirty-ratio threshold for tile execution ────────────────────────
    // When the dirty area covers >threshold of the screen, per-tile graph
    // re-execution overhead dominates and single-pass is faster.  This threshold
    // prevents pathological slowdowns on scenes with large dirty regions.
    const bool dirty_ratio_below_threshold =
        dirty_ratio <= settings.tile_dirty_ratio_threshold;

    const bool use_tile_execution = tile_safe &&
                                    dirty_out.use_dirty_tiles &&
                                    dirty_ratio_below_threshold &&
                                    sw_renderer &&
                                    sw_renderer->executor() &&
                                    dirty_out.tile_grid &&
                                    dirty_out.dirty_tiles &&
                                    dirty_out.dirty_tiles->any();

    if (ctx.diagnostics_enabled && !dirty_ratio_below_threshold) {
        spdlog::info(
            "[tile-debug] frame={} tile_execution_skipped dirty_ratio={:.3f} threshold={} reason=high_dirty_ratio",
            static_cast<int>(frame), dirty_ratio, settings.tile_dirty_ratio_threshold);
    }

    if (use_tile_execution) {
        // ── Allocate final framebuffer: copy previous frame for clean tiles ──
        {
            CHRONON_ZONE_C("tile_acquire", trace_category::kFrame);
            const bool have_prev = sw_renderer->m_prev_framebuffer &&
                                   sw_renderer->m_prev_framebuffer->width() == width &&
                                   sw_renderer->m_prev_framebuffer->height() == height;
            if (have_prev) {
                fb_shared = ctx.acquire_framebuffer(*sw_renderer->m_prev_framebuffer);
            } else {
                fb_shared = ctx.acquire_framebuffer(width, height, true);
            }
        }

        // ── Tile execution (parallel or sequential) ─────────────────────────
        {
            CHRONON_ZONE_C("tile_execute", trace_category::kGraph);
            const int total_tiles = dirty_out.tile_grid->tile_count();
            const bool parallel_tiles = sw_renderer->settings().enable_parallel_tiles;

            auto tile_result = detail::execute_dirty_tiles(
                compiled, ctx, sw_renderer, dirty_out,
                *fb_shared, width, height, parallel_tiles);

            // ── Tile counters ───────────────────────────────────────────────
            if (ctx.counters) {
                ctx.counters->tile_dirty_count.fetch_add(tile_result.dirty_count, std::memory_order_relaxed);
                const int clean_count = std::max(0, total_tiles - tile_result.dirty_count);
                ctx.counters->tile_clean_count.fetch_add(clean_count, std::memory_order_relaxed);
                ctx.counters->tile_pixels_rendered.fetch_add(tile_result.pixels_rendered, std::memory_order_relaxed);
                const uint64_t total_pixels =
                    static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
                const uint64_t pixels_skipped = (total_pixels > tile_result.pixels_rendered)
                    ? total_pixels - tile_result.pixels_rendered : 0;
                ctx.counters->tile_pixels_skipped.fetch_add(pixels_skipped, std::memory_order_relaxed);
            }

            if (ctx.diagnostics_enabled) {
                spdlog::info(
                    "[tile-debug] frame={} tile_total={} tile_dirty={}",
                    static_cast<int>(frame), total_tiles, tile_result.dirty_count);
            }
        }
    } else {
        // ── Traditional single-pass execution ───────────────────────────────
        {
            CHRONON_ZONE_C("graph_execute", trace_category::kGraph);
            if (sw_renderer && sw_renderer->executor()) {
                fb_shared = sw_renderer->executor()->execute(compiled, ctx);
            } else {
                GraphExecutor local_executor;
                fb_shared = local_executor.execute(compiled, ctx);
            }
        }
        // Track tile fallbacks when tile system requested but couldn't execute
        if (dirty_out.use_dirty_tiles && ctx.counters) {
            ctx.counters->tile_full_fallbacks.fetch_add(1, std::memory_order_relaxed);
        }
    }
    const auto t_exec1 = std::chrono::steady_clock::now();

    if (ctx.counters || ctx.diagnostics_enabled) {
        const double resolve_ms = std::chrono::duration<double, std::milli>(t_resolve1 - t_resolve0).count();
        const double dirty_ms = std::chrono::duration<double, std::milli>(t_dirty1 - t_dirty0).count();
        const double graph_ms = std::chrono::duration<double, std::milli>(t_graph1 - t_graph0).count();
        const double exec_ms = std::chrono::duration<double, std::milli>(t_exec1 - t_exec0).count();
        const double total_graph_ms = resolve_ms + dirty_ms + graph_ms + exec_ms;
        if (ctx.counters) {
            ctx.counters->graph_resolve_layers_ms.fetch_add(to_ms_u64(resolve_ms), std::memory_order_relaxed);
            ctx.counters->graph_dirty_rect_ms.fetch_add(to_ms_u64(dirty_ms), std::memory_order_relaxed);
            ctx.counters->graph_build_ms.fetch_add(to_ms_u64(graph_ms), std::memory_order_relaxed);
            ctx.counters->graph_execute_ms.fetch_add(to_ms_u64(exec_ms), std::memory_order_relaxed);
            ctx.counters->graph_total_ms.fetch_add(to_ms_u64(total_graph_ms), std::memory_order_relaxed);
        }
        if (ctx.diagnostics_enabled) {
            spdlog::info(
                "[graph-timing] frame={} resolve_layers_ms={:.2f} dirty_rect_ms={:.2f} graph_phase_ms={:.2f} graph_exec_ms={:.2f} graph_total_ms={:.2f} graph_reused={}",
                static_cast<int>(frame),
                resolve_ms,
                dirty_ms,
                graph_ms,
                exec_ms,
                total_graph_ms,
                graph_reused ? 1 : 0
            );
        }
    }

    // ── Save state for next frame ───────────────────────────────────────
    // Use combined_fp = static_fp ^ (active_at_fp * C) to match the fingerprint
    // stored in the resolved-reuse block above.  This ensures consistent fingerprint
    // comparison across frames (both fast-path and full-path save the same format).
    if (sw_renderer) {
        // Cache render graph for incremental reuse next frame
        sw_renderer->m_cached_compiled_graph = std::make_unique<CompiledFrameGraph>(std::move(compiled));
        sw_renderer->m_cached_compiled_width = width;
        sw_renderer->m_cached_compiled_height = height;
        sw_renderer->m_cached_compiled_structure_hash = sw_renderer->m_cached_compiled_graph->structure_hash;

        const uint64_t save_static_fp = sw_renderer->m_scene_hasher.compute_static_fingerprint(scene);
        const uint64_t save_structure_fp = sw_renderer->m_scene_hasher.compute_structure_fingerprint(scene);
        const uint64_t save_active_at_fp = sw_renderer->m_scene_hasher.compute_active_at_fingerprint(scene, frame);
        const uint64_t save_combined_fp = save_static_fp ^ (save_active_at_fp * 0x9e3779b97f4a7c15ULL);
        sw_renderer->m_prev_framebuffer = fb_shared;
        sw_renderer->m_prev_layer_bboxes = std::move(dirty_out.layer_bboxes);
        sw_renderer->m_prev_frame = frame;
        sw_renderer->m_prev_scene_fingerprint = save_combined_fp;
        sw_renderer->m_prev_static_scene_fingerprint = save_static_fp;
        sw_renderer->m_prev_graph_structure_fingerprint = save_structure_fp;
        sw_renderer->m_prev_active_at_fingerprint = save_active_at_fp;
        sw_renderer->m_prev_camera = resolved.camera.camera;
        sw_renderer->m_prev_camera_valid = resolved.camera.camera.enabled;
    }
    const auto hits_after = node_cache.stats().hits;

    return fb_shared;
}

} // namespace chronon3d::graph
