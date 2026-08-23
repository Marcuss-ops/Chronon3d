#include "execution_state.hpp"
#include "cache_evaluator.hpp"
#include "node_runner.hpp"
#include "node_executor.hpp"         // P1 step 2 — run_node extraction (node.execute + OwnedFB→CachedFB + scratch/renderer/pool deleter + cache write)
#include "node_skip_policy.hpp"      // P1 §5 unified skip policy (commit_transparent_skip + SkipReason)
#include "node_state_commit.hpp"      // P1 step 3 — commit_node_state (5 state-slot commit helper; byte-equivalent to the inline Sites 1+3 pattern at lines 221-225 + 403-407)
#include "tile_pruning.hpp"
#include "telemetry_emitter.hpp"
#include "text_bbox_reconcile.hpp"   // P0 #1 extracted post-render alpha reconciliation
#include <chronon3d/cache/persistent_framebuffer_store.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/internal/render_graph/node_memory_tracker.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/render_graph/nodes/composite_node.hpp>
#include <chronon3d/render_graph/nodes/text_run_node.hpp>
#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/backends/text/text_render_resources.hpp>
#include <chronon3d/text/glyph_atlas.hpp>
#include <blend2d.h>
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/runtime/gpu_text_atlas_cache.hpp>
#include <chronon3d/runtime/gpu_layer_batch.hpp>
#include <chronon3d/media/media_placement.hpp>
#include "../nodes/native_surface.hpp"
#include "../nodes/text_run/text_run_execution.hpp"
#include <spdlog/spdlog.h>
#include <cmath>
#include <unordered_map>

namespace chronon3d::graph {

namespace {

[[nodiscard]] std::string memory_node_id(
    const CompiledFrameGraph& compiled,
    GraphNodeId id,
    std::string_view fallback) {
    if (id < compiled.nodes.size() && compiled.nodes[id].reachable &&
        compiled.graph_instance_id != kInvalidGraphInstanceId &&
        compiled.nodes[id].stable_node_id != kInvalidStableNodeId) {
        return "g" + std::to_string(compiled.graph_instance_id.value) +
               ":n" + std::to_string(compiled.nodes[id].stable_node_id.value);
    }
    return std::string(fallback);
}

void execute_fused_batch(
    ExecutionState& state,
    RenderGraph& graph,
    RenderGraphContext& ctx,
    const CompiledLayerBatch& batch,
    GraphNodeId id,
    FramebufferPool* parent_pool,
    RenderCounters* parent_counters,
    const CompiledFrameGraph& compiled) {
    (void)parent_pool;
    if (!ctx.services.backend || batch.instances.empty()) return;

    bool is_text_batch = false;
    for (const auto& inst : batch.instances) {
        if (inst.node < compiled.nodes.size() && compiled.nodes[inst.node].kind == RenderGraphNodeKind::TextRun) {
            is_text_batch = true;
            break;
        }
    }

    if (is_text_batch) {
#ifdef CHRONON3D_ENABLE_TEXT
        std::vector<runtime::GlyphStatic> all_glyphs;
        std::vector<runtime::TextRunDynamic> all_runs;
        std::vector<runtime::RenderSurfaceHandle> atlas_pages;
        std::unordered_map<runtime::RenderSurfaceHandle, uint32_t> atlas_map;

        for (const auto& inst : batch.instances) {
            if (inst.node >= graph.size() || !graph.has_node(inst.node)) continue;
            const auto& node = graph.node(inst.node);
            if (node.kind() != RenderGraphNodeKind::TextRun) continue;
            const auto& tr_node = static_cast<const TextRunNode&>(node);
            if (!tr_node.shape()) continue;

            float tx = 0.0f, ty = 0.0f, sx = 1.0f, sy = 1.0f;
            float opacity = inst.opacity;
            if (inst.transform_index != 0 && inst.transform_index < graph.size()) {
                const auto& xform = static_cast<const TransformNode&>(graph.node(inst.transform_index));
                const auto m = xform.matrix();
                tx = m[3][0];
                ty = m[3][1];
                sx = m[0][0];
                sy = m[1][1];
                opacity *= xform.opacity();
            }

            TextRunShape local_shape = text_run::prepare_per_frame_shape(*tr_node.shape(), ctx.frame_input.sample_time);
            if (!local_shape.layout) continue;

            const auto& layout = *local_shape.layout;
            const int font_size = std::max(1, static_cast<int>(std::lround(layout.font_size)));

            runtime::TextRunDynamic run_dyn{};
            run_dyn.tx = tx;
            run_dyn.ty = ty;
            run_dyn.sx = sx;
            run_dyn.sy = sy;
            run_dyn.opacity = opacity;
            run_dyn.color = 0xFFFFFFFF;
            all_runs.push_back(run_dyn);

            for (std::size_t gi = 0; gi < layout.placed.glyphs.size(); ++gi) {
                const auto& gstate = local_shape.glyphs[gi];
                if (gstate.glyph_id == 0) continue;
                const auto& placed = layout.placed.glyphs[gi];
                if (placed.bbox_x1 <= placed.bbox_x0) continue;

                if (!ctx.services.text_render_resources) continue;
                auto entry = ctx.services.text_render_resources->lookup_glyph_atlas(
                    layout.font.font_path, gstate.glyph_id, static_cast<u32>(font_size));
                if (!entry || !entry->image) continue;

                runtime::RenderSurfaceHandle page_handle = 1;
                uint32_t page_idx = 0;
                auto it = atlas_map.find(page_handle);
                if (it != atlas_map.end()) {
                    page_idx = it->second;
                } else {
                    page_idx = static_cast<uint32_t>(atlas_pages.size());
                    atlas_pages.push_back(page_handle);
                    atlas_map[page_handle] = page_idx;
                }

                runtime::GlyphStatic g_stat{};
                g_stat.run_index = static_cast<uint32_t>(all_runs.size() - 1);
                g_stat.atlas_page = static_cast<uint16_t>(page_idx);
                g_stat.flags = 0;
                g_stat.atlas_x = static_cast<uint16_t>(entry->x_offset >= 0 ? entry->x_offset : 0);
                g_stat.atlas_y = static_cast<uint16_t>(entry->y_offset >= 0 ? entry->y_offset : 0);
                g_stat.atlas_w = static_cast<uint16_t>(entry->image->width());
                g_stat.atlas_h = static_cast<uint16_t>(entry->image->height());
                g_stat.plane_left = static_cast<float>(placed.x);
                g_stat.plane_top = static_cast<float>(placed.y);
                g_stat.plane_right = static_cast<float>(placed.x + entry->image->width());
                g_stat.plane_bottom = static_cast<float>(placed.y + entry->image->height());
                g_stat.draw_order = static_cast<uint32_t>(all_glyphs.size());
                all_glyphs.push_back(g_stat);
            }
        }

        auto dest_fb = ctx.acquire_framebuffer(ctx.frame_input.width, ctx.frame_input.height, true);
        ensure_native_surface(ctx, *dest_fb);
        ctx.services.backend->draw_text_batch(dest_fb->surface_handle(), all_glyphs, all_runs, atlas_pages);

        state.temp[id] = dest_fb;
        state.resolved_bboxes[id] = raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
        if (parent_counters) {
            parent_counters->nodes_executed.fetch_add(1, std::memory_order_relaxed);
        }
#endif
        return;
    }

    // Image Batch
    std::vector<runtime::LayerInstance> instances;
    std::vector<runtime::RenderSurfaceHandle> resources;
    std::unordered_map<runtime::RenderSurfaceHandle, uint32_t> resource_map;

    for (const auto& inst : batch.instances) {
        if (inst.node >= graph.size() || !graph.has_node(inst.node)) continue;
        const auto& node = graph.node(inst.node);
        if (node.kind() != RenderGraphNodeKind::Source) continue;
        const auto& src_node = static_cast<const SourceNode&>(node);
        if (src_node.render_node().shape.type() != ShapeType::Image) continue;
        const auto& img = src_node.render_node().shape.image();
        if (img.path.empty() || !ctx.services.image_cache || !ctx.services.gpu_asset_cache) continue;

        const auto cached = ctx.services.image_cache->find(img.path, img.decode_options);
        if (!cached || !cached->valid() || cached->gpu_rgba.empty() || !cached->fb_img) continue;

        const auto& key = cached->gpu_key;
        const runtime::SurfaceDesc desc{
            key.width, key.height, key.format,
            runtime::ResourceUsage::Storage,
            runtime::LifetimeClass::JobPersistent,
            cached->gpu_rgba.size() * sizeof(float)};
        const auto acquired = ctx.services.gpu_asset_cache->acquire(key, desc, cached->gpu_rgba);
        if (!acquired.ok()) continue;

        runtime::RenderSurfaceHandle handle = acquired.handle;
        uint32_t res_idx = 0;
        auto it = resource_map.find(handle);
        if (it != resource_map.end()) {
            res_idx = it->second;
        } else {
            res_idx = static_cast<uint32_t>(resources.size());
            resources.push_back(handle);
            resource_map[handle] = res_idx;
        }

        float tx = 0.0f, ty = 0.0f;
        float opacity = inst.opacity;
        if (inst.transform_index != 0 && inst.transform_index < graph.size()) {
            const auto& xform = static_cast<const TransformNode&>(graph.node(inst.transform_index));
            const auto m = xform.matrix();
            tx = m[3][0];
            ty = m[3][1];
            opacity *= xform.opacity();
        }

        const Vec2 original_source_size{static_cast<float>(cached->fb_img->width()), static_cast<float>(cached->fb_img->height())};
        const auto placement = compute_media_placement(original_source_size, img.size, img.fit, img.focal_point);

        const float world_x0 = tx - img.size.x * 0.5f + placement.dst_rect.origin.x + (ctx.frame_input.width * 0.5f);
        const float world_y0 = ty - img.size.y * 0.5f + placement.dst_rect.origin.y + (ctx.frame_input.height * 0.5f);
        const float world_x1 = world_x0 + placement.dst_rect.size.x;
        const float world_y1 = world_y0 + placement.dst_rect.size.y;

        runtime::LayerInstance gpu_inst{};
        gpu_inst.resource_index = res_idx;
        gpu_inst.dst_x0 = world_x0;
        gpu_inst.dst_y0 = world_y0;
        gpu_inst.dst_x1 = world_x1;
        gpu_inst.dst_y1 = world_y1;
        gpu_inst.src_x0 = placement.src_rect.origin.x;
        gpu_inst.src_y0 = placement.src_rect.origin.y;
        gpu_inst.src_x1 = placement.src_rect.origin.x + placement.src_rect.size.x;
        gpu_inst.src_y1 = placement.src_rect.origin.y + placement.src_rect.size.y;
        gpu_inst.opacity = opacity;
        gpu_inst.blend = BlendMode::Normal;
        gpu_inst.kind = runtime::PrimitiveKind::Image;
        instances.push_back(gpu_inst);
    }

    auto dest_fb = ctx.acquire_framebuffer(ctx.frame_input.width, ctx.frame_input.height, true);
    ensure_native_surface(ctx, *dest_fb);
    ctx.services.backend->execute_layer_batch(dest_fb->surface_handle(), instances, resources, {}, {});

    state.temp[id] = dest_fb;
    state.resolved_bboxes[id] = raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
    if (parent_counters) {
        parent_counters->nodes_executed.fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace

void execute_single_node(
    ExecutionState& state,
    RenderGraph& graph,
    RenderGraphContext& ctx,
    const std::pmr::vector<PreResolvedNode>& level_resolved,
    GraphNodeId id,
    size_t level_index,
    RenderCounters* parent_counters,
    FramebufferPool* parent_pool,
    std::pmr::vector<std::atomic_size_t>& consumer_remaining,
    double* out_cache_ms,
    double* out_dirty_ms,
    double* out_telemetry_ms,
    double* out_execute_ms,
    double* out_predicted_bbox_ms,
    double* out_clone_context_ms,
    double* out_state_assign_ms,
    const CompiledFrameGraph& compiled
) {
    // Hoist cheap per-node scalar reads (O(1) vector lookups) to the top so
    // both early-out guards below can inspect them. Moving them earlier is
    // free when the guard doesn't fire (the body of execute_single_node
    // reuses them once again) and required when it does.
    auto& node = graph.node(id);
    const auto& input_ids = graph.inputs(id);
    const auto& pr = level_resolved[level_index];

    // ── TICKET-FIX-ALPHA-SCANNER-DUP-V1 — wire per-session reporter ─
    // Forward `ExecutionState::text_bbox_reporter` into the per-node
    // mutable workspace so node-level diagnostics (e.g. TextRunNode's
    // pre-render `suspiciously_thin` + FU04 guards) can dedup their
    // warn-once via the canonical per-session pattern instead of
    // process-wide `static bool warned = false`.  Set once per
    // execute_single_node call so parallel level processing shares
    // the SAME reporter instance (intentional: the per-session
    // guarantee is per-ExecutionState, not per-node).
    ctx.node_exec.text_bbox_reporter = &state.text_bbox_reporter;

    // ── WP 4.3 — populate per-node identity ────────────────────────────────
    // Stamp `ctx.node_exec.current_identity` with this node's
    // `(graph_instance_id, stable_node_id)` BEFORE cloning the per-node
    // context.  `clone_for_node_execution()` propagates the field through
    // the per-node copy so the clone that the node sees carries the
    // identity required by PrecompNode::execute() and downstream
    // instrumentation.
    if (id < compiled.nodes.size() && compiled.nodes[id].reachable
        && compiled.nodes[id].stable_node_id != kInvalidStableNodeId) {
        ctx.node_exec.current_identity = NodeIdentity{
            compiled.graph_instance_id,
            compiled.nodes[id].stable_node_id
        };
        // Processor bindings are installed only on the node-local clone below;
        // writing them into the shared frame context would race when a level
        // executes nodes concurrently.
    } else {
        // Leave processor bindings unset on the shared context. The node-local
        // clone receives the complete binding contract below.
    }

    if (id < ctx.node_exec.early_exit_skip.size() && ctx.node_exec.early_exit_skip[id]) {
        commit_transparent_skip(
            state, id, ctx, parent_pool,
            SkipReason::EarlyExit,
            graph.node(id).name());
        return;
    }

    // ── Phase 4 — interior static node skip ───────────────────────────
    // Nodes whose output has been pre-baked in prepare() are skipped
    // entirely — no execute, no cache eval, no clone.  Their output
    // lives in the baked surface owned by the prepared program.
    if (id < compiled.program.interior_node_skip.size() &&
        compiled.program.interior_node_skip[id]) {
        if (ctx.node_exec.counters) {
            ctx.node_exec.counters->nodes_executed.fetch_add(1, std::memory_order_relaxed);
        }
        commit_transparent_skip(
            state, id, ctx, parent_pool,
            SkipReason::StaticBaked,
            graph.node(id).name());
        return;
    }

    // ── Phase B — GPU Fused Layer / Text Batch Execution ──────────────
    if (id < compiled.nodes.size() && compiled.nodes[id].lowered_into_batch &&
        ctx.services.backend && ctx.services.backend->is_batching_supported()) {
        const CompiledLayerBatch* batch_for_root = nullptr;
        for (const auto& b : compiled.program.layer_batches) {
            if (b.is_gpu_fused && b.root_node == id) {
                batch_for_root = &b;
                break;
            }
        }
        if (!batch_for_root) {
            // Internal member node — skip execution without emitting legacy ops
            commit_transparent_skip(
                state, id, ctx, parent_pool,
                SkipReason::StaticBaked,
                graph.node(id).name());
            return;
        }

        // Execute the fused batch at the root node
        execute_fused_batch(state, graph, ctx, *batch_for_root, id, parent_pool, parent_counters, compiled);
        return;
    }

    profiling::ProfilingGuard node_guard(parent_counters, parent_pool);

    // node, input_ids, pr already declared at the top of this function so
    // both early-out guards above can read them.

    const auto t_cache0 = profiling::now();
    auto cache_eval = evaluate_cache(
        node, ctx,
        pr.input_hash,
        pr.inputs_frame_dependent,
        pr.has_cacheable_inputs,
        id
    );
    const auto t_cache1 = profiling::now();
    if (out_cache_ms) {
        *out_cache_ms = profiling::duration_ms(t_cache0, t_cache1);
    }

    if (ctx.policy.diagnostics_enabled) {
        spdlog::debug("[DIAG-exec] frame={} node='{}' id={} kind='{}' cache='{}' frame_dep={} use_cache={} result_ptr={}",
            static_cast<int>(ctx.frame_input.frame), node.name(), id, to_string(node.kind()),
            cache_eval.cache_status, cache_eval.node_frame_dependent ? 1 : 0,
            cache_eval.use_cache ? 1 : 0,
            cache_eval.result ? fmt::ptr(cache_eval.result.get()) : "null");
    }

    const auto t_bbox0 = profiling::now();
    auto predicted_bbox = node.predicted_bbox(ctx, pr.input_bboxes);
    const auto t_bbox1 = profiling::now();
    if (out_predicted_bbox_ms) {
        *out_predicted_bbox_ms = profiling::duration_ms(t_bbox0, t_bbox1);
    }

    const bool cache_hit_fast_path =
        cache_eval.result &&
        cache_eval.cache_status == "hit" &&
        !cache_eval.node_frame_dependent &&
        !ctx.policy.tile_execution_enabled;

    if (cache_hit_fast_path) {
        const auto t_fast0 = profiling::now();

        commit_node_state(state, id, cache_eval, predicted_bbox);

        const auto t_fast1 = profiling::now();
        const double fast_duration_ms = profiling::duration_ms(t_fast0, t_fast1);

        const auto t_telemetry0 = profiling::now();
        emit_node_records(
            ctx, node,
            cache_eval.key,
            cache_eval.result,
            predicted_bbox,
            cache_eval.cache_status,
            cache_eval.is_cacheable,
            static_cast<int>(input_ids.size()),
            fast_duration_ms
        );
        const auto t_telemetry1 = profiling::now();
        if (ctx.node_exec.counters) {
            ctx.node_exec.counters->nodes_executed.fetch_add(1, std::memory_order_relaxed);
        }
        if (out_telemetry_ms) {
            *out_telemetry_ms = profiling::duration_ms(t_telemetry0, t_telemetry1);
        }
        if (out_cache_ms) {
            *out_cache_ms = profiling::duration_ms(t_cache0, t_cache1);
        }
        if (out_dirty_ms) {
            *out_dirty_ms = 0.0;
        }
        if (out_execute_ms) {
            *out_execute_ms = 0.0;
        }
        if (out_clone_context_ms) {
            *out_clone_context_ms = 0.0;
        }
        if (out_state_assign_ms) {
            *out_state_assign_ms = 0.0;
        }
        return;
    }

    if (ctx.policy.tile_execution_enabled && ctx.node_exec.active_tile_clip &&
        predicted_bbox && !predicted_bbox->is_empty())
    {
        const auto& tile = *ctx.node_exec.active_tile_clip;
        const auto& bbox = *predicted_bbox;
        const bool bbox_intersects_tile =
            bbox.x0 < tile.x1 && bbox.x1 > tile.x0 &&
            bbox.y0 < tile.y1 && bbox.y1 > tile.y0;
        if (!bbox_intersects_tile) {
            // TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX: instrada attraverso la skip-policy
            // unificata invece del blocco manuale (saved Cat-3 single SSoT:
            // riusa state.shared_transparent, no fresh 64×64 alloc, bump
            // `nodes_skipped` invece di duplicare il pattern).
            commit_transparent_skip(
                state, id, ctx, parent_pool, SkipReason::TilePruned,
                /*node_name=*/{}, /*bbox_override=*/predicted_bbox);
            return;
        }
    }

    const auto t_clone0 = profiling::now();
    RenderGraphContext node_ctx = ctx.clone_for_node_execution();
    node_ctx.node_exec.planned_physical_slot = nullptr;
    if (id < compiled.physical_framebuffer_plan.resources.size()) {
        const auto& allocation = compiled.physical_framebuffer_plan.resources[id];
        if (allocation.aliasable &&
            allocation.physical_slot != kInvalidPhysicalFramebufferSlot &&
            allocation.physical_slot < state.physical_slots.size()) {
            node_ctx.node_exec.planned_physical_slot =
                &state.physical_slots[allocation.physical_slot];
        }
    }
    // Bind processor metadata on the node-local clone, not on the shared
    // frame context. Nodes in the same execution level may run concurrently;
    // writing the shared span before cloning allowed one node to overwrite
    // another node's binding and made effect execution appear unbound.
    if (id < compiled.nodes.size() && compiled.nodes[id].reachable) {
        node_ctx.node_exec.current_identity = NodeIdentity{
            compiled.graph_instance_id,
            compiled.nodes[id].stable_node_id
        };
        const auto& node_info = compiled.nodes[id];
        node_ctx.node_exec.current_shape_processor = {};
        node_ctx.node_exec.current_shape_processors = {};
        node_ctx.node_exec.current_effect_processors = {};
        node_ctx.node_exec.processor_snapshot = compiled.processor_snapshot;
        if (node_info.shape_processors_count > 0 &&
            node_info.shape_processors_offset <= compiled.shape_processor_table.size() &&
            node_info.shape_processors_count <=
                compiled.shape_processor_table.size() - node_info.shape_processors_offset) {
            const auto* shape_begin = compiled.shape_processor_table.data() +
                node_info.shape_processors_offset;
            node_ctx.node_exec.current_shape_processors = {
                shape_begin, node_info.shape_processors_count};
            node_ctx.node_exec.current_shape_processor = shape_begin[0];
        }
        if (node_info.effect_processors_count > 0 &&
            node_info.effect_processors_offset <= compiled.effect_processor_table.size() &&
            node_info.effect_processors_count <=
                compiled.effect_processor_table.size() - node_info.effect_processors_offset) {
            const auto* effect_begin = compiled.effect_processor_table.data() +
                node_info.effect_processors_offset;
            node_ctx.node_exec.current_effect_processors = {
                effect_begin, node_info.effect_processors_count};
        }
        node_ctx.node_exec.processor_bindings_compiled = true;
    } else {
        node_ctx.node_exec.current_shape_processor = {};
        node_ctx.node_exec.current_shape_processors = {};
        node_ctx.node_exec.current_effect_processors = {};
        node_ctx.node_exec.processor_snapshot = compiled.processor_snapshot;
        node_ctx.node_exec.processor_bindings_compiled = false;
    }
    const auto t_clone1 = profiling::now();
    if (out_clone_context_ms) {
        *out_clone_context_ms = profiling::duration_ms(t_clone0, t_clone1);
    }

    const auto t_dirty0 = profiling::now();
    if (ctx.policy.dirty_rects_enabled) {
        node_ctx.node_exec.clip_rect = compute_dirty_clip(ctx, node, predicted_bbox);
    } else {
        node_ctx.node_exec.clip_rect = predicted_bbox;
    }
    const auto t_dirty1 = profiling::now();
    if (out_dirty_ms) {
        *out_dirty_ms = profiling::duration_ms(t_dirty0, t_dirty1);
    }

    node_ctx.node_exec.reusable_inputs.clear();
    node_ctx.node_exec.reusable_bottom.reset();
    for (size_t j = 0; j < input_ids.size(); ++j) {
        const GraphNodeId input_id = input_ids[j];
        if (contains_index(state.temp, input_id) && state.temp[input_id]) {
            const bool compiled_sole_consumer =
                input_id < compiled.ownership_transfers.size() &&
                compiled.ownership_transfers[input_id].transferable &&
                compiled.ownership_transfers[input_id].consumer == id;
            if ((compiled_sole_consumer ||
                 consumer_remaining[input_id].load(std::memory_order_relaxed) == 1) &&
                state.temp[input_id].use_count() == 1) {
                node_ctx.node_exec.reusable_inputs.push_back(state.temp[input_id].get());
                // The FIRST input (j == 0) is the "bottom" in composite
                // terminology — pre-cache_skip, CompositeNode calls
                // `acquire_owned_fb(*bottom)` which used to invoke
                // `pool->acquire_from(other)` (8 MB memcpy).  By saving
                // the CachedFB here, `acquire_owned_fb(const FB&)` can
                // do a 1×1-placeholder pixel swap with the ORIGINAL
                // PoolFbDeleter instead.
                if (j == 0) {
                    node_ctx.node_exec.reusable_bottom = state.temp[input_id];
                }
            }
        }
    }

    std::optional<TemporalSampleKey> sample_key;
    if (ctx.frame_input.temporal_key != TemporalSampleKey{}) {
        sample_key = ctx.frame_input.temporal_key;
    }
    std::optional<ScopedNodeMemory> node_memory_scope;
    if (state.node_memory_tracker) {
        node_memory_scope.emplace(
            *state.node_memory_tracker,
            memory_node_id(compiled, id, node.name()),
            sample_key,
            0);
    }

    const double duration_ms = run_node(
        node, node_ctx,
        pr.inputs, pr.input_bboxes,
        cache_eval.use_cache,
        cache_eval.key,
        cache_eval.result,
        ctx,
        parent_pool,
        &compiled.nodes[id].stable_node_id  // trace annotation (plan §7)
    );
    if (out_execute_ms) {
        *out_execute_ms = duration_ms;
    }
    if (state.node_memory_tracker && cache_eval.result) {
        node_memory_scope->set_live_bytes(cache_eval.result->size_bytes());
    }

    // TICKET-SIMPLICITY-CONSERVATIVE-BBOX — F1.C post-render alpha_bbox
    // expansion has been removed.  The predicted bbox is now computed
    // from FreeType outline bboxes (see src/backends/text/font_engine.cpp
    // and src/text/text_run_geometry.cpp), which account for glyph ink
    // extents including negative side-bearings and descenders.  The call
    // to `reconcile_text_bbox_after_render()` is retained for ABI
    // compatibility but it now always returns std::nullopt.
    //
    // Gated on: TextRun node kind, successful render (non-null fb),
    // non-empty predicted_bbox.
    if (node.kind() == RenderGraphNodeKind::TextRun &&
        cache_eval.result && predicted_bbox && !predicted_bbox->is_empty())
    {
        const Framebuffer* fb_ptr = cache_eval.result.get();
        if (fb_ptr && fb_ptr->width() > 0 && fb_ptr->height() > 0) {
            if (auto expanded_bbox =
                    reconcile_text_bbox_after_render(
                        node, *fb_ptr, predicted_bbox,
                        ctx.node_exec.counters,
                        state.text_bbox_reporter,
                        node_ctx.node_exec.actual_ink_bbox))
            {
                predicted_bbox = *expanded_bbox;
            }
        }
    }

    const auto t_telemetry0 = profiling::now();
    emit_node_records(
        ctx, node,
        cache_eval.key,
        cache_eval.result,
        predicted_bbox,
        cache_eval.cache_status,
        cache_eval.is_cacheable,
        static_cast<int>(input_ids.size()),
        duration_ms
    );
    const auto t_telemetry1 = profiling::now();
    if (out_telemetry_ms) {
        *out_telemetry_ms = profiling::duration_ms(t_telemetry0, t_telemetry1);
    }

    commit_node_state(state, id, cache_eval, predicted_bbox);

    const auto t_state1 = profiling::now();
    if (out_state_assign_ms) {
        *out_state_assign_ms = profiling::duration_ms(t_telemetry1, t_state1);
    }
}

} // namespace chronon3d::graph
