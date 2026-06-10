#include "execution_state.hpp"
#include "cache_evaluator.hpp"
#include "node_runner.hpp"
#include "tile_pruning.hpp"
#include "telemetry_emitter.hpp"
#include <chronon3d/cache/disk_node_cache.hpp>
#include <chronon3d/cache/persistent_bake_cache.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/math/color.hpp>
#include <spdlog/spdlog.h>
#include <chrono>
#include <cmath>

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

    const auto exec_t0 = std::chrono::steady_clock::now();
    OwnedFB owned;
    {
        CHRONON_ZONE_C("node_execute", trace_category::kGraph);
        owned = node.execute(node_ctx, inputs, input_bboxes);
    }
    if (ctx.telemetry.counters) {
        ctx.telemetry.counters->nodes_executed.fetch_add(1, std::memory_order_relaxed);
    }
    if (owned) {
        owned->set_key_digest(key.digest());

        // ── Transform scratch buffer: preserve the scratch_slot deleter ──
        //    through the CachedFB so the buffer is restored to the scratch
        //    slot (cleared) when all consumers finish, instead of being
        //    released to the pool.  Also skip caching — caching the scratch
        //    would allow stale content to survive past the frame boundary.
        const bool is_scratch = owned.get_deleter().scratch_slot != nullptr;

        if (is_scratch) {
            // Preserve the scratch deleter with its scratch_slot pointer.
            // This ensures the buffer is cleared and returned to the slot
            // when the last shared_ptr reference is dropped (arena cleanup).
            PoolFbDeleter scratch_deleter = std::move(owned.get_deleter());
            Framebuffer* raw = owned.release();
            result = CachedFB(raw, std::move(scratch_deleter));
        } else if (owned.get_deleter().owned_by_renderer) {
            // Renderer-owned FB (e.g., ping-pong buffer): preserve the no-op
            // deleter so the buffer is neither deleted nor returned to the pool.
            // The renderer manages lifetime explicitly via m_ping_fb[].
            PoolFbDeleter noop;
            noop.owned_by_renderer = true;
            Framebuffer* raw = owned.release();
            result = CachedFB(raw, std::move(noop));
        } else {
            Framebuffer* raw = owned.release();
            PoolFbDeleter deleter{nullptr};
            if (parent_pool) {
                deleter = PoolFbDeleter{parent_pool, parent_pool->alive_token()};
            }
            result = CachedFB(raw, std::move(deleter));
        }

        if (use_cache && ctx.resources.node_cache && !is_scratch) {
            ctx.resources.node_cache->store(key, result);
            if (node.cache_policy().disk_cacheable && disk_node_cache_enabled_for_current_run()) {
                cache::DiskNodeCache::instance().put(key, *result);
            }
        }
    }
    const auto exec_t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(exec_t1 - exec_t0).count();
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
    double* out_state_assign_ms
) {
    if (id < ctx.tile.early_exit_skip.size() && ctx.tile.early_exit_skip[id]) {
        auto owned_fb = ctx.acquire_owned_fb(64, 64, false);
        owned_fb->clear(Color::transparent());
        Framebuffer* raw = owned_fb.release();
        PoolFbDeleter deleter{nullptr};
        if (parent_pool) {
            deleter = PoolFbDeleter{parent_pool, parent_pool->alive_token()};
        }
        state.temp[id] = CachedFB(raw, std::move(deleter));
        state.resolved_key_digest[id] = 0;
        state.resolved_frame_dependent[id] = 0;
        state.resolved_cache_hit[id] = 0;
        state.resolved_bboxes[id] = raster::BBox{0, 0, 0, 0};
        if (ctx.telemetry.counters) {
            ctx.telemetry.counters->layers_culled.fetch_add(1, std::memory_order_relaxed);
            if (graph.node(id).name() == "Clear") {
                ctx.telemetry.counters->clear_skipped_calls.fetch_add(1, std::memory_order_relaxed);
                const uint64_t clear_pixels = static_cast<uint64_t>(ctx.frame.width) * static_cast<uint64_t>(ctx.frame.height);
                ctx.telemetry.counters->clear_skipped_pixels.fetch_add(clear_pixels, std::memory_order_relaxed);
            }
        }
        return;
    }

    profiling::ProfilingGuard node_guard(parent_counters, parent_pool);

    auto& node = graph.node(id);
    const auto& input_ids = graph.inputs(id);
    const auto& pr = level_resolved[level_index];

    const auto t_cache0 = std::chrono::steady_clock::now();
    auto cache_eval = evaluate_cache(
        node, ctx,
        pr.input_hash,
        pr.inputs_frame_dependent,
        pr.has_cacheable_inputs,
        id,
        pr.inputs_all_cache_hits
    );
    const auto t_cache1 = std::chrono::steady_clock::now();
    if (out_cache_ms) {
        *out_cache_ms = std::chrono::duration<double, std::milli>(t_cache1 - t_cache0).count();
    }

    if (ctx.options.diagnostics_enabled) {
        spdlog::debug("[DIAG-exec] frame={} node='{}' id={} kind='{}' cache='{}' frame_dep={} use_cache={} result_ptr={}",
            static_cast<int>(ctx.frame.frame), node.name(), id, to_string(node.kind()),
            cache_eval.cache_status, cache_eval.node_frame_dependent ? 1 : 0,
            cache_eval.use_cache ? 1 : 0,
            cache_eval.result ? fmt::ptr(cache_eval.result.get()) : "null");
    }

    const auto t_bbox0 = std::chrono::steady_clock::now();
    auto predicted_bbox = node.predicted_bbox(ctx, pr.input_bboxes);
    const auto t_bbox1 = std::chrono::steady_clock::now();
    if (out_predicted_bbox_ms) {
        *out_predicted_bbox_ms = std::chrono::duration<double, std::milli>(t_bbox1 - t_bbox0).count();
    }

    const bool cache_hit_fast_path =
        cache_eval.result &&
        cache_eval.cache_status == "hit" &&
        !cache_eval.node_frame_dependent &&
        !ctx.tile.tile_execution_enabled;

    if (cache_hit_fast_path) {
        const auto t_fast0 = std::chrono::steady_clock::now();

        state.temp[id] = cache_eval.result;
        state.resolved_key_digest[id] = cache_eval.key.digest();
        state.resolved_frame_dependent[id] = 0;
        state.resolved_cache_hit[id] = 1;
        state.resolved_bboxes[id] = predicted_bbox;

        const auto t_fast1 = std::chrono::steady_clock::now();
        const double fast_duration_ms = std::chrono::duration<double, std::milli>(t_fast1 - t_fast0).count();

        const auto t_telemetry0 = std::chrono::steady_clock::now();
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
        const auto t_telemetry1 = std::chrono::steady_clock::now();
        if (ctx.telemetry.counters) {
            ctx.telemetry.counters->nodes_executed.fetch_add(1, std::memory_order_relaxed);
        }
        if (out_telemetry_ms) {
            *out_telemetry_ms = std::chrono::duration<double, std::milli>(t_telemetry1 - t_telemetry0).count();
        }
        if (out_cache_ms) {
            *out_cache_ms = std::chrono::duration<double, std::milli>(t_cache1 - t_cache0).count();
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

    if (ctx.tile.tile_execution_enabled && ctx.tile.active_tile_clip &&
        predicted_bbox && !predicted_bbox->is_empty())
    {
        const auto& tile = *ctx.tile.active_tile_clip;
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

            if (ctx.telemetry.counters) {
                ctx.telemetry.counters->nodes_skipped.fetch_add(1, std::memory_order_relaxed);
            }
            return;
        }
    }

    // Use lightweight clone that skips copying large vectors
    // (node_telemetry, layer_telemetry, dof_depth, early_exit_skip).
    // These are not read during node.execute() — the full copy was the
    // #1 bottleneck (~20K ms overhead for 90 node executions).
    const auto t_clone0 = std::chrono::steady_clock::now();
    auto node_ctx = ctx.clone_for_node_execution();
    const auto t_clone1 = std::chrono::steady_clock::now();
    if (out_clone_context_ms) {
        *out_clone_context_ms = std::chrono::duration<double, std::milli>(t_clone1 - t_clone0).count();
    }

    const auto t_dirty0 = std::chrono::steady_clock::now();
    if (ctx.options.dirty_rects_enabled) {
        node_ctx.tile.clip_rect = compute_dirty_clip(ctx, node, predicted_bbox);
    } else {
        node_ctx.tile.clip_rect = predicted_bbox;
    }
    const auto t_dirty1 = std::chrono::steady_clock::now();
    if (out_dirty_ms) {
        *out_dirty_ms = std::chrono::duration<double, std::milli>(t_dirty1 - t_dirty0).count();
    }

    node_ctx.scratch.reusable_inputs.clear();
    for (size_t j = 0; j < input_ids.size(); ++j) {
        const GraphNodeId input_id = input_ids[j];
        if (contains_index(state.temp, input_id) && state.temp[input_id]) {
            if (consumer_remaining[input_id].load(std::memory_order_relaxed) == 1 &&
                state.temp[input_id].use_count() == 1) {
                node_ctx.scratch.reusable_inputs.push_back(state.temp[input_id].get());
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

    const auto t_telemetry0 = std::chrono::steady_clock::now();
    emit_node_records(
        ctx, node,
        cache_eval.key,
        cache_eval.result,
        node_ctx.tile.clip_rect,
        cache_eval.cache_status,
        cache_eval.is_cacheable,
        static_cast<int>(input_ids.size()),
        duration_ms
    );
    const auto t_telemetry1 = std::chrono::steady_clock::now();
    if (out_telemetry_ms) {
        *out_telemetry_ms = std::chrono::duration<double, std::milli>(t_telemetry1 - t_telemetry0).count();
    }

    state.temp[id] = cache_eval.result;
    state.resolved_key_digest[id] = cache_eval.key.digest();
    state.resolved_frame_dependent[id] = cache_eval.node_frame_dependent ? 1 : 0;
    state.resolved_cache_hit[id] = (cache_eval.cache_status == "hit") ? 1 : 0;
    state.resolved_bboxes[id] = predicted_bbox;

    const auto t_state1 = std::chrono::steady_clock::now();
    if (out_state_assign_ms) {
        *out_state_assign_ms = std::chrono::duration<double, std::milli>(t_state1 - t_telemetry1).count();
    }
}

} // namespace chronon3d::graph
