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
#include <chronon3d/render_graph/optimizer/graph_optimizer.hpp>
#include <chronon3d/render_graph/compiler/frame_graph_compiler.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/multi_source_node.hpp>
#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
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

[[nodiscard]] LayerGraphItem make_layer_graph_item_for_refresh(
    const ResolvedLayer& resolved_layer,
    const RenderGraphContext& ctx
) {
    const Layer& layer = *resolved_layer.layer;

    if (ctx.camera_2_5d.enabled && layer.uses_2_5d_projection) {
        Transform effective_transform = resolved_layer.world_transform;
        const Mat4 projection_world_matrix = effective_transform.to_mat4();
        auto proj = project_layer_2_5d(
            effective_transform,
            projection_world_matrix,
            ctx.camera_2_5d,
            static_cast<f32>(ctx.width),
            static_cast<f32>(ctx.height),
            ctx.diagnostics_enabled
        );
        if (proj.visible) {
            const Mat4 eff_proj = detail::is_native_3d_layer(layer)
                ? Mat4(1.0f)
                : proj.projection_matrix;
            return LayerGraphItem{
                .layer             = resolved_layer.layer,
                .transform         = proj.transform,
                .world_matrix      = resolved_layer.world_matrix,
                .projection_matrix = eff_proj,
                .depth             = proj.depth,
                .world_z           = resolved_layer.world_transform.position.z,
                .projected         = true,
                .native_3d         = detail::is_native_3d_layer(layer),
                .insertion_index   = resolved_layer.insertion_index,
                .matte_node        = k_invalid_node,
                .is_static         = layer.cache_static,
            };
        }
    }

    return LayerGraphItem{
        .layer           = resolved_layer.layer,
        .transform       = resolved_layer.world_transform,
        .world_matrix    = resolved_layer.world_matrix,
        .depth           = 0.0f,
        .world_z         = resolved_layer.world_transform.position.z,
        .projected       = false,
        .native_3d       = detail::is_native_3d_layer(layer),
        .insertion_index = resolved_layer.insertion_index,
        .matte_node      = k_invalid_node,
        .is_static       = layer.cache_static,
    };
}

void refresh_compiled_graph_payloads(
    CompiledFrameGraph& compiled,
    const Scene& scene,
    RenderGraphContext& ctx,
    const detail::LayerResolutionResult& resolved
) {
    std::unordered_map<std::string, const ResolvedLayer*> resolved_by_name;
    resolved_by_name.reserve(resolved.layers.size());
    for (const auto& rl : resolved.layers) {
        if (rl.layer) {
            resolved_by_name.emplace(std::string(rl.layer->name), &rl);
        }
    }

    std::unordered_map<std::string, const RenderNode*> root_nodes_by_name;
    root_nodes_by_name.reserve(scene.nodes().size());
    for (const auto& node : scene.nodes()) {
        root_nodes_by_name.emplace(std::string(node.name), &node);
    }

    const auto refresh_source_node = [&](SourceNode& node) {
        const std::string layer_id = node.layer_id();
        if (layer_id.empty()) {
            const auto it = root_nodes_by_name.find(node.name());
            if (it == root_nodes_by_name.end()) return;
            const RenderNode& src_node = *it->second;
            cache::NodeCacheKey key{
                .scope = "root.source:" + std::string(src_node.name),
                .frame = ctx.frame,
                .width = ctx.width,
                .height = ctx.height,
                .params_hash = hash_render_node(src_node),
                .source_hash = hash_bytes(src_node.name.data(), src_node.name.size())
            };
            node.refresh(
                std::string(src_node.name),
                src_node,
                key,
                ctx.modular_coordinates
            );
            return;
        }

        const auto layer_it = resolved_by_name.find(layer_id);
        if (layer_it == resolved_by_name.end() || !layer_it->second || !layer_it->second->layer) {
            return;
        }

        const ResolvedLayer& rl = *layer_it->second;
        const Layer& layer = *rl.layer;
        if (layer.kind != LayerKind::Normal || layer.nodes.size() != 1) {
            return;
        }

        const auto& src_node = layer.nodes[0];
        const LayerGraphItem item = make_layer_graph_item_for_refresh(rl, ctx);
        const bool use_local = ctx.modular_coordinates &&
            detail::layer_needs_render_transform(item, ctx) &&
            !item.native_3d;
        const Mat4 item_source_world = use_local
            ? item.world_matrix
            : detail::source_space_world_matrix(item, ctx);
        const Mat4 node_matrix = src_node.world_transform.to_mat4();
        const Mat4 render_matrix = use_local
            ? node_matrix
            : (item_source_world * node_matrix);
        const f32 render_opacity = use_local
            ? src_node.world_transform.opacity
            : (item.transform.opacity * src_node.world_transform.opacity);
        cache::NodeCacheKey key{
            .scope = "layer.source:" + std::string(layer.name) + ":" + std::string(src_node.name),
            .frame = layer.cache_static ? Frame{0} : ctx.frame,
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = hash_render_node(src_node),
            .source_hash = hash_bytes(src_node.name.data(), src_node.name.size())
        };

        node.refresh(
            std::string(src_node.name),
            src_node,
            key,
            detail::should_use_centered_rendering(item, ctx),
            item.projected,
            ctx.modular_coordinates ? std::optional<Mat4>(render_matrix) : std::nullopt,
            ctx.modular_coordinates ? std::optional<f32>(render_opacity) : std::nullopt,
            layer.cache_static
        );
    };

    const auto refresh_multi_source_node = [&](MultiSourceNode& node) {
        const std::string layer_id = node.layer_id();
        const auto layer_it = resolved_by_name.find(layer_id);
        if (layer_it == resolved_by_name.end() || !layer_it->second || !layer_it->second->layer) {
            return;
        }

        const ResolvedLayer& rl = *layer_it->second;
        const Layer& layer = *rl.layer;
        if (layer.kind != LayerKind::Normal || layer.nodes.size() <= 1) {
            return;
        }

        const LayerGraphItem item = make_layer_graph_item_for_refresh(rl, ctx);
        const bool use_local = ctx.modular_coordinates &&
            detail::layer_needs_render_transform(item, ctx) &&
            !item.native_3d;
        const Mat4 item_source_world = use_local
            ? item.world_matrix
            : detail::source_space_world_matrix(item, ctx);

        std::vector<MultiSourceItem> items;
        items.reserve(layer.nodes.size());
        u64 aggregated_params_hash = 0;
        for (const auto& src_node : layer.nodes) {
            const Mat4 node_matrix = src_node.world_transform.to_mat4();
            const Mat4 render_matrix = use_local
                ? node_matrix
                : (item_source_world * node_matrix);
            const f32 render_opacity = use_local
                ? src_node.world_transform.opacity
                : (item.transform.opacity * src_node.world_transform.opacity);

            items.push_back(MultiSourceItem{
                .node = &src_node,
                .matrix = render_matrix,
                .opacity = render_opacity
            });
            aggregated_params_hash = hash_combine(aggregated_params_hash, hash_render_node(src_node));
        }

        cache::NodeCacheKey key{
            .scope = "layer.multisource:" + std::string(layer.name),
            .frame = layer.cache_static ? Frame{0} : ctx.frame,
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = aggregated_params_hash,
            .source_hash = hash_string(std::string(layer.name) + "_multisource")
        };

        node.refresh(
            std::string(layer.name) + "_multi",
            std::move(items),
            key,
            detail::should_use_centered_rendering(item, ctx),
            item.projected,
            layer.cache_static
        );
    };

    const auto refresh_effect_stack_node = [&](EffectStackNode& node) {
        const std::string layer_id = node.layer_id();
        if (layer_id.empty()) return;
        const auto layer_it = resolved_by_name.find(layer_id);
        if (layer_it == resolved_by_name.end() || !layer_it->second || !layer_it->second->layer) {
            return;
        }
        const Layer& layer = *layer_it->second->layer;
        // Re-evaluate animated blur at the current frame and update BlurParams.
        if (layer.anim_transform.blur.is_animated()) {
            const Frame local_frame = layer.local_frame(ctx.frame);
            const f32 blur_radius = layer.anim_transform.blur.evaluate(local_frame);
            for (auto& effect : node.effects()) {
                if (auto* blur = std::get_if<BlurParams>(&effect.params)) {
                    blur->radius = blur_radius;
                }
            }
        }
    };

    const auto refresh_transform_node = [&](TransformNode& node) {
        const std::string layer_id = node.layer_id();
        if (layer_id.empty()) {
            return;
        }

        const auto layer_it = resolved_by_name.find(layer_id);
        if (layer_it == resolved_by_name.end() || !layer_it->second || !layer_it->second->layer) {
            return;
        }

        const ResolvedLayer& rl = *layer_it->second;
        const LayerGraphItem item = make_layer_graph_item_for_refresh(rl, ctx);

        if (item.projected) {
            node.set_matrix(item.projection_matrix);
            node.set_opacity(item.transform.opacity);
        } else {
            Mat4 effective_matrix = item.world_matrix;
            if (ctx.ssaa_factor > 1.0f) {
                Mat4 ssaa_world = item.world_matrix;
                ssaa_world[3][0] *= ctx.ssaa_factor;
                ssaa_world[3][1] *= ctx.ssaa_factor;
                effective_matrix = ssaa_world;
            }
            node.set_matrix(effective_matrix);
            node.set_opacity(item.transform.opacity);
        }
    };

    for (size_t id = 0; id < compiled.graph.size(); ++id) {
        if (!compiled.graph.has_node(static_cast<GraphNodeId>(id))) {
            continue;
        }

        auto& graph_node = compiled.graph.node(static_cast<GraphNodeId>(id));
        if (auto* source_node = dynamic_cast<SourceNode*>(&graph_node)) {
            refresh_source_node(*source_node);
        } else if (auto* multi_source_node = dynamic_cast<MultiSourceNode*>(&graph_node)) {
            refresh_multi_source_node(*multi_source_node);
        } else if (auto* effect_node = dynamic_cast<EffectStackNode*>(&graph_node)) {
            refresh_effect_stack_node(*effect_node);
        } else if (auto* transform_node = dynamic_cast<TransformNode*>(&graph_node)) {
            refresh_transform_node(*transform_node);
        }
    }
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

        refresh_compiled_graph_payloads(compiled, scene, ctx, resolved);
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
        // Full build path — construct the render graph
        {
            CHRONON_ZONE_C("build_graph", trace_category::kGraph);
            auto mutable_ctx = ctx;
            auto built_graph = detail::build_graph(scene, mutable_ctx, resolved);
            ctx.skip_initial_clear = mutable_ctx.skip_initial_clear;
            ctx.early_exit_skip = std::move(mutable_ctx.early_exit_skip);
            graph = std::move(built_graph);
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

    if (ctx.diagnostics_enabled) {
        const double resolve_ms = std::chrono::duration<double, std::milli>(t_resolve1 - t_resolve0).count();
        const double dirty_ms = std::chrono::duration<double, std::milli>(t_dirty1 - t_dirty0).count();
        const double graph_ms = std::chrono::duration<double, std::milli>(t_graph1 - t_graph0).count();
        spdlog::info(
            "[graph-timing] frame={} resolve_layers_ms={:.2f} dirty_rect_ms={:.2f} graph_phase_ms={:.2f} graph_reused={}",
            static_cast<int>(frame),
            resolve_ms,
            dirty_ms,
            graph_ms,
            graph_reused ? 1 : 0
        );
    }

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
    const bool use_tile_execution = tile_safe &&
                                    dirty_out.use_dirty_tiles &&
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
                    compiled, tile_ctx);

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
