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
#include "../builder/graph_builder_internal.hpp"
#include <chronon3d/render_graph/optimizer/graph_optimizer.hpp>
#include <chronon3d/core/tile_grid.hpp>
#include <chronon3d/core/dirty_tile_mask.hpp>
#include "helpers.hpp"
#include "scene_internal.hpp"
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace chronon3d::graph {

namespace {

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

} // namespace

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
    if (scene.camera_2_5d().enabled) {
        ctx.camera_2_5d = scene.camera_2_5d();
        ctx.has_camera_2_5d = true;
        ctx.projection_ctx = renderer::make_projection_context(
            ctx.camera_2_5d, ctx.width, ctx.height);
        ctx.projection_ctx.ready = true;
    }

    profiling::g_current_counters = ctx.counters;

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
                ctx.counters->clear_skipped_calls.fetch_add(1, std::memory_order_relaxed);
                ctx.counters->clear_skipped_pixels.fetch_add(
                    static_cast<uint64_t>(width) * height,
                    std::memory_order_relaxed
                );
            }
            profiling::g_current_counters = nullptr;
            return sw_renderer->m_prev_framebuffer;
        }
    }

    // ── Full path: resolve, dirty rect, graph, execute ──────────────────
    if (settings.diagnostic_plan) {
        auto* prev_counters = profiling::g_current_counters;
        profiling::g_current_counters = nullptr;
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
        profiling::g_current_counters = prev_counters;
    }

    const auto resolved = detail::resolve_layers(scene, ctx);

    auto dirty_out = detail::compute_dirty_rect(
        ctx, resolved, scene, settings, sw_renderer, frame, width, height);

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
        profiling::g_current_counters = nullptr;
        return sw_renderer->m_prev_framebuffer;
    }

    // ── Build + optimize render graph (once, shared across tile loop) ──
    RenderGraph graph = [&]() {
        CHRONON_ZONE_C("build_graph", trace_category::kGraph);
        auto mutable_ctx = ctx;
        auto built_graph = detail::build_graph(scene, mutable_ctx, resolved);
        // build_graph mutates optimization hints that the executor needs later.
        ctx.skip_initial_clear = mutable_ctx.skip_initial_clear;
        ctx.early_exit_skip = std::move(mutable_ctx.early_exit_skip);
        return built_graph;
    }();

    if (ctx.diagnostics_enabled) {
        for (size_t i = 0; i < graph.size(); ++i) {
            if (!graph.has_node(i)) continue;
            std::string inputs_str = "";
            for (auto in : graph.inputs(i)) {
                inputs_str += std::to_string(in) + " (" + graph.node(in).name() + "), ";
            }
            spdlog::info("[graph-structure] node_id={} name='{}' inputs=[{}]", i, graph.node(i).name(), inputs_str);
        }
    }

    // Apply graph-level optimizations (node fusion, branch pruning, static bake analysis)
    {
        CHRONON_ZONE_C("optimize_graph", trace_category::kGraph);
        auto opt_result = optimizer::optimize_graph(graph, ctx);
        if (ctx.diagnostics_enabled && (opt_result.nodes_fused > 0 || opt_result.nodes_pruned > 0 ||
                                        opt_result.effects_fused > 0 || opt_result.dead_nodes_removed > 0)) {
            spdlog::info("[graph-opt] frame={} before={} after={} fused={} pruned={} effects_fused={} dead_removed={} baked={}",
                static_cast<int>(frame), opt_result.nodes_before, opt_result.nodes_after,
                opt_result.nodes_fused, opt_result.nodes_pruned,
                opt_result.effects_fused, opt_result.dead_nodes_removed,
                opt_result.static_bakes);
        }
    }
    const auto t_build2 = std::chrono::steady_clock::now();

    // ── Execute: tile-based (V1) or traditional single-pass ─────────────
    const auto t_exec0 = std::chrono::steady_clock::now();
    std::shared_ptr<Framebuffer> fb_shared;
    const bool use_tile_execution = dirty_out.use_dirty_tiles &&
                                    sw_renderer &&
                                    sw_renderer->executor() &&
                                    dirty_out.tile_grid &&
                                    dirty_out.dirty_tiles &&
                                    dirty_out.dirty_tiles->any();

    if (use_tile_execution) {
        const auto& tile_grid = *dirty_out.tile_grid;
        const auto& dirty_tiles = *dirty_out.dirty_tiles;

        // ── Allocate final framebuffer: copy previous frame for clean tiles ──
        {
            CHRONON_ZONE_C("tile_acquire", trace_category::kFrame);
            const bool have_prev = sw_renderer->m_prev_framebuffer &&
                                   sw_renderer->m_prev_framebuffer->width() == width &&
                                   sw_renderer->m_prev_framebuffer->height() == height;
            if (have_prev) {
                // Deep copy from previous framebuffer, allocating from the current
                // arena pool.  The copy-constructor of arena-backed Framebuffers only
                // copies the external pointer (shallow), causing all tile-execution
                // frames to share the same arena memory.  When that arena is recycled
                // the stale pointer produces visual glitches.
                fb_shared = ctx.acquire_framebuffer(*sw_renderer->m_prev_framebuffer);
            } else {
                fb_shared = ctx.acquire_framebuffer(width, height, true);
            }
        }

        // ── Tile execution loop (parallel) ───────────────────────────────
        {
            CHRONON_ZONE_C("tile_execute", trace_category::kGraph);
            const int total_tiles = tile_grid.tile_count();

            // Sequential tile loop.  Each tile renders the full graph
            // independently with a tile-scoped clip_rect, then the tile
            // region is copied into the final framebuffer.  The sequential
            // outer loop avoids TBB nesting / thread-local state issues
            // while the executor still parallelises internally per node level.
            int dirty_count = 0;
            uint64_t pixels_rendered = 0;

            dirty_tiles.for_each_dirty_tile(tile_grid, [&](int tx, int ty) {
                const raster::BBox tile_bbox = tile_grid.tile_bounds(tx, ty);
                if (tile_bbox.is_empty()) return;

                // Clone context with tile-level clipping.
                // Disable prev-fb reuse so the ClearNode allocates a
                // fresh framebuffer instead of stealing m_prev_framebuffer.
                // Per-tile cache keys prevent stale cross-tile cache hits.
                RenderGraphContext tile_ctx = ctx;
                tile_ctx.clip_rect = tile_bbox;
                tile_ctx.dirty_rect = tile_bbox;
                tile_ctx.reuse_prev_framebuffer = false;
                tile_ctx.tile_execution_enabled = true;
                tile_ctx.active_tile_clip = tile_bbox;
                tile_ctx.skip_initial_clear = false;
                tile_ctx.early_exit_skip.clear();
                // Reset optimisations that the graph builder may have
                // applied for the full-frame path; tile execution needs
                // ClearNode to zero each tile region and all nodes to
                // execute normally per tile.

                auto tile_fb = sw_renderer->executor()->execute(
                    graph, graph.output(), tile_ctx);

                // Direct row copy — the tile replaces old frame
                // content in its region (no alpha blending, since
                // fb_shared was seeded from prev_framebuffer).
                if (tile_fb) {
                    for (i32 y = tile_bbox.y0; y < tile_bbox.y1; ++y) {
                        std::copy(
                            tile_fb->pixels_row(y) + tile_bbox.x0,
                            tile_fb->pixels_row(y) + tile_bbox.x1,
                            fb_shared->pixels_row(y) + tile_bbox.x0);
                    }
                }

                ++dirty_count;
                pixels_rendered +=
                    static_cast<uint64_t>(tile_bbox.x1 - tile_bbox.x0) *
                    static_cast<uint64_t>(tile_bbox.y1 - tile_bbox.y0);
            });

            const int final_dirty = dirty_count;
            const uint64_t final_px = pixels_rendered;

            // ── Tile counters ───────────────────────────────────────────────
            if (ctx.counters) {
                ctx.counters->tile_dirty_count.fetch_add(final_dirty, std::memory_order_relaxed);
                const int clean_count = std::max(0, total_tiles - final_dirty);
                ctx.counters->tile_clean_count.fetch_add(clean_count, std::memory_order_relaxed);
                ctx.counters->tile_pixels_rendered.fetch_add(final_px, std::memory_order_relaxed);
                const uint64_t total_pixels =
                    static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
                const uint64_t pixels_skipped = (total_pixels > final_px)
                    ? total_pixels - final_px : 0;
                ctx.counters->tile_pixels_skipped.fetch_add(pixels_skipped, std::memory_order_relaxed);
            }

            if (ctx.diagnostics_enabled) {
                spdlog::info(
                    "[tile-debug] frame={} tile_total={} tile_dirty={}",
                    static_cast<int>(frame), total_tiles, final_dirty);
            }
        }
    } else {
        // ── Traditional single-pass execution ───────────────────────────────
        {
            CHRONON_ZONE_C("graph_execute", trace_category::kGraph);
            if (sw_renderer && sw_renderer->executor()) {
                fb_shared = sw_renderer->executor()->execute(graph, graph.output(), ctx);
            } else {
                GraphExecutor local_executor;
                fb_shared = local_executor.execute(graph, graph.output(), ctx);
            }
        }
        // Track tile fallbacks when tile system requested but couldn't execute
        if (dirty_out.use_dirty_tiles && ctx.counters) {
            ctx.counters->tile_full_fallbacks.fetch_add(1, std::memory_order_relaxed);
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

    profiling::g_current_counters = nullptr;
    return fb_shared;
}

} // namespace chronon3d::graph
