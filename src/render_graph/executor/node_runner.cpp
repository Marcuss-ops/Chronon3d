#include "execution_state.hpp"
#include "cache_evaluator.hpp"
#include "node_runner.hpp"
#include "tile_pruning.hpp"
#include "telemetry_emitter.hpp"
#include "text_bbox_reconcile.hpp"   // P0 #1 extracted post-render alpha reconciliation
#include <chronon3d/cache/persistent_framebuffer_store.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <spdlog/spdlog.h>
#include <cmath>
#include <cstdlib>

namespace chronon3d::graph {

double run_node(
    RenderGraphNode& node,
    RenderGraphContext& node_ctx,
    std::span<const FramebufferRef> inputs,
    std::span<const std::optional<raster::BBox>> input_bboxes,
    bool use_cache,
    const cache::NodeCacheKey& key,
    CachedFB& result,
    const RenderGraphContext& ctx,
    cache::FramebufferPool* parent_pool
) {
    if (result) {
        return 0.001;
    }

    const auto exec_t0 = profiling::now();
    OwnedFB owned;
    {
        CHRONON_ZONE_C("node_execute", trace_category::kGraph);
        auto exec_result = node.execute(node_ctx, inputs, input_bboxes);
        if (!exec_result) {
            // P0-1 — node execution failed; write error to the shared
            // frame_error slot so the executor can propagate it to
            // frame-level failure (GraphExecutor returns nullptr).
            if (ctx.frame_error) {
                *ctx.frame_error = exec_result.error();
            }
            if (ctx.node_exec.counters) {
                ctx.node_exec.counters->nodes_executed.fetch_add(1, std::memory_order_relaxed);
            }
            const auto exec_t1 = profiling::now();
            return profiling::duration_ms(exec_t0, exec_t1);
        }
        owned = exec_result.take_value();
    }
    if (ctx.node_exec.counters) {
        ctx.node_exec.counters->nodes_executed.fetch_add(1, std::memory_order_relaxed);
    }
    if (owned) {
        owned->set_key_digest(key.digest());

        // ── Transform scratch buffer: preserve the scratch_slot deleter ──
        //    through the CachedFB so the buffer is restored to the scratch
        //    slot (cleared) when all consumers finish, instead of being
        //    released to the pool.  Also skip caching — caching the scratch
        //    would allow stale content to survive past the frame boundary.
        const bool is_scratch = std::holds_alternative<ReturnToScratch>(owned.get_deleter().policy);

        if (is_scratch) {
            // Preserve the scratch deleter with its scratch_slot pointer.
            // This ensures the buffer is cleared and returned to the slot
            // when the last shared_ptr reference is dropped (arena cleanup).
            PoolFbDeleter scratch_deleter = std::move(owned.get_deleter());
            Framebuffer* raw = owned.release();
            result = CachedFB(raw, std::move(scratch_deleter));
        } else if (std::holds_alternative<RendererOwned>(owned.get_deleter().policy)) {
            // Renderer-owned FB (e.g., ping-pong buffer): preserve the no-op
            // deleter so the buffer is neither deleted nor returned to the pool.
            // The renderer manages lifetime explicitly via RendererBufferRing.
            PoolFbDeleter noop;
            noop.policy = RendererOwned{};
            Framebuffer* raw = owned.release();
            result = CachedFB(raw, std::move(noop));
        } else {
            Framebuffer* raw = owned.release();
            PoolFbDeleter deleter;
            if (parent_pool) {
                deleter = PoolFbDeleter{parent_pool->shared_from_this()};
            }
            result = CachedFB(raw, std::move(deleter));
        }

        if (use_cache && ctx.services.node_cache && !is_scratch) {
            ctx.services.node_cache->store(key, result);
            // persistent framebuffer cache removed (framebuffer_store → framebuffer_pool)
            if (node.cache_policy().persistent() && persistent_framebuffer_cache_enabled_for_current_run()) {
                // ctx.services.framebuffer_pool->put() no longer exists
            }
        }
    }
    const auto exec_t1 = profiling::now();
    return profiling::duration_ms(exec_t0, exec_t1);
}

void execute_single_node(
    ExecutionState& state,
    RenderGraph& graph,
    RenderGraphContext& ctx,
    const std::pmr::vector<PreResolvedNode>& level_resolved,
    GraphNodeId id,
    size_t level_index,
    RenderCounters* parent_counters,
    cache::FramebufferPool* parent_pool,
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
    }

    if (id < ctx.node_exec.early_exit_skip.size() && ctx.node_exec.early_exit_skip[id]) {
        auto owned_fb = ctx.acquire_owned_fb(64, 64, false);
        owned_fb->clear(Color::transparent());
        Framebuffer* raw = owned_fb.release();
        PoolFbDeleter deleter;
        if (parent_pool) {
            deleter = PoolFbDeleter{parent_pool->shared_from_this()};
        }
        state.temp[id] = CachedFB(raw, std::move(deleter));
        state.resolved_key_digest[id] = 0;
        state.resolved_frame_dependent[id] = 0;
        state.resolved_cache_hit[id] = 0;
        state.resolved_bboxes[id] = raster::BBox{0, 0, 0, 0};
        if (ctx.node_exec.counters) {
            ctx.node_exec.counters->layers_culled.fetch_add(1, std::memory_order_relaxed);
            if (graph.node(id).name() == "Clear") {
                ctx.node_exec.counters->clear_skipped_calls.fetch_add(1, std::memory_order_relaxed);
                const uint64_t clear_pixels = static_cast<uint64_t>(ctx.frame_input.width) * static_cast<uint64_t>(ctx.frame_input.height);
                ctx.node_exec.counters->clear_skipped_pixels.fetch_add(clear_pixels, std::memory_order_relaxed);
            }
        }
        return;
    }

    // ── Opacity-threshold early-out (env-gated; OPT-OUT by default) ──
    // For text-heavy compositions with many per-letter staggered layers
    // (cinematic_text_camera's WhipPanHeroReveal, AbyssFreefallStagger) the
    // per-layer orchestrator + transform + composite work costs ~10-30 ms per
    // invisible layer. When the layer's effective opacity is below 0.1 %
    // AND none of its blur/scale/transform animations are time-dependent we
    // skip the full execute_single_node body and emit a 64x64 transparent fb
    // (mirroring the early_exit_skip path above).
    //
    // Defaults to OFF. Set CHRONON3D_SKIP_INVISIBLE_LAYERS=1 once the populate
    // site in executor_levels.cpp::resolve_level has been wired to evaluate
    // each node's layer.opacity_anim() into pr.resolved_opacity — the field
    // exists today but defaults to 1.0f so this guard is currently a no-op.
    static const bool kSkipInvisibleOpacity = []() -> bool {
        const char* e = std::getenv("CHRONON3D_SKIP_INVISIBLE_LAYERS");
        if (!e) return false;
        return e[0] == '1' && e[1] == '\0';
    }();
    if (kSkipInvisibleOpacity && pr.resolved_opacity <= 0.001f) {
        auto owned_fb_inv = ctx.acquire_owned_fb(64, 64, false);
        owned_fb_inv->clear(Color::transparent());
        Framebuffer* raw_inv = owned_fb_inv.release();
        PoolFbDeleter deleter_inv;
        if (parent_pool) {
            deleter_inv = PoolFbDeleter{parent_pool->shared_from_this()};
        }
        state.temp[id] = CachedFB(raw_inv, std::move(deleter_inv));
        state.resolved_key_digest[id] = 0;
        state.resolved_frame_dependent[id] = 0;
        state.resolved_cache_hit[id] = 0;
        state.resolved_bboxes[id] = raster::BBox{0, 0, 0, 0};
        if (ctx.node_exec.counters) {
            ctx.node_exec.counters->layers_culled.fetch_add(1, std::memory_order_relaxed);
        }
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

        state.temp[id] = cache_eval.result;
        state.resolved_key_digest[id] = cache_eval.key.digest();
        state.resolved_frame_dependent[id] = 0;
        state.resolved_cache_hit[id] = 1;
        state.resolved_bboxes[id] = predicted_bbox;

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
            state.temp[id] = state.shared_transparent;
            state.resolved_key_digest[id] = 0;
            state.resolved_frame_dependent[id] = 0;
            state.resolved_cache_hit[id] = 0;
            state.resolved_bboxes[id] = predicted_bbox;

            if (ctx.node_exec.counters) {
                ctx.node_exec.counters->nodes_skipped.fetch_add(1, std::memory_order_relaxed);
            }
            return;
        }
    }

    // Use lightweight clone that skips copying large vectors
    // (node_telemetry, layer_telemetry, dof_depth, early_exit_skip).
    // These are not read during node.execute() — the full copy was the
    // #1 bottleneck (~20K ms overhead for 90 node executions).
    const auto t_clone0 = profiling::now();
    auto node_ctx = ctx.clone_for_node_execution();
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
            if (consumer_remaining[input_id].load(std::memory_order_relaxed) == 1 &&
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

    const double duration_ms = run_node(
        node, node_ctx,
        pr.inputs, pr.input_bboxes,
        cache_eval.use_cache,
        cache_eval.key,
        cache_eval.result,
        ctx,
        parent_pool
    );
    if (out_execute_ms) {
        *out_execute_ms = duration_ms;
    }

    // TICKET-SIMPLICITY-CONSERVATIVE-BBOX — F1.C post-render alpha_bbox
    // expansion.  After a TextRun node renders, delegate to
    // `reconcile_text_bbox_after_render()` (P0 #1 extracted) which uses
    // the canonical `alpha_bbox_scan()` from
    // `<chronon3d/text/alpha_bbox_scanner.hpp>` to compute the actual
    // ink bounding box.  If the actual ink extends beyond the predicted
    // bbox, expand resolved_bboxes[id] so the compositor doesn't clip
    // visible text.  This catches cases where predicted_bbox was
    // technically valid (finite, non-empty) but under-estimated the
    // glyph ink extent (e.g., tight font metrics, unexpected kerning,
    // large descenders).
    //
    // P0 #1 co-located effects:
    //   (a) Closes the duplicate alpha-framebuffer scan (previously a
    //       TU-local peak-pixel loop in this file, now centralised in
    //       `text_bbox_reconcile.{hpp,cpp}`).
    //   (b) Closes the data-race-prone `static bool warned = false;`
    //       pattern via the function-local `std::atomic<bool>` in the
    //       helper (lock-free, thread-safe exactly-once).
    //   (c) Closes the historical early-exit bug
    //       `if (y > alpha_y1 + 2 && alpha_y1 >= alpha_y0) break;`
    //       (lost second lines / subtitles / far-down descenders) —
    //       the canonical scanner never early-exits.
    //
    // Gated on: TextRun node kind, successful render (non-null fb),
    // non-empty predicted_bbox (otherwise we already fall back to full
    // canvas in the pre-render guard).
    if (node.kind() == RenderGraphNodeKind::TextRun &&
        cache_eval.result && predicted_bbox && !predicted_bbox->is_empty())
    {
        const Framebuffer* fb_ptr = cache_eval.result.get();
        if (fb_ptr && fb_ptr->width() > 0 && fb_ptr->height() > 0) {
            if (auto expanded_bbox =
                    reconcile_text_bbox_after_render(
                        node, *fb_ptr, predicted_bbox,
                        ctx.node_exec.counters))
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

    state.temp[id] = cache_eval.result;
    state.resolved_key_digest[id] = cache_eval.key.digest();
    state.resolved_frame_dependent[id] = cache_eval.node_frame_dependent ? 1 : 0;
    state.resolved_cache_hit[id] = (cache_eval.cache_status == "hit") ? 1 : 0;
    state.resolved_bboxes[id] = predicted_bbox;

    const auto t_state1 = profiling::now();
    if (out_state_assign_ms) {
        *out_state_assign_ms = profiling::duration_ms(t_telemetry1, t_state1);
    }
}

} // namespace chronon3d::graph
