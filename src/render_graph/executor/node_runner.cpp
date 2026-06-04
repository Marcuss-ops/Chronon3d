#include "execution_state.hpp"
#include "internal.hpp"
#include <chronon3d/cache/disk_node_cache.hpp>
#include <chronon3d/cache/persistent_bake_cache.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/math/color.hpp>
#include <spdlog/spdlog.h>
#include <chrono>

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
    if (ctx.counters) {
        ctx.counters->nodes_executed.fetch_add(1, std::memory_order_relaxed);
    }
    if (owned) {
        owned->set_key_digest(key.digest());

        if (use_cache && ctx.node_cache) {
            Framebuffer* raw = owned.release();
            PoolFbDeleter deleter{nullptr};
            if (parent_pool) {
                deleter = PoolFbDeleter{parent_pool, parent_pool->alive_token()};
            }
            result = CachedFB(raw, std::move(deleter));
            ctx.node_cache->store(key, result);
            if (node.cache_policy().disk_cacheable && disk_node_cache_enabled_for_current_run()) {
                cache::DiskNodeCache::instance().put(key, *result);
            }
        } else {
            Framebuffer* raw = owned.release();
            PoolFbDeleter deleter{nullptr};
            if (parent_pool) {
                deleter = PoolFbDeleter{parent_pool, parent_pool->alive_token()};
            }
            result = CachedFB(raw, std::move(deleter));
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
    std::pmr::vector<std::atomic_size_t>& consumer_remaining
) {
    if (id < ctx.early_exit_skip.size() && ctx.early_exit_skip[id]) {
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
        if (ctx.counters) {
            ctx.counters->layers_culled.fetch_add(1, std::memory_order_relaxed);
            if (graph.node(id).name() == "Clear") {
                ctx.counters->clear_skipped_calls.fetch_add(1, std::memory_order_relaxed);
                const uint64_t clear_pixels = static_cast<uint64_t>(ctx.width) * static_cast<uint64_t>(ctx.height);
                ctx.counters->clear_skipped_pixels.fetch_add(clear_pixels, std::memory_order_relaxed);
            }
        }
        return;
    }

    profiling::ProfilingGuard node_guard(parent_counters, parent_pool);

    auto& node = graph.node(id);
    const auto& input_ids = graph.inputs(id);
    const auto& pr = level_resolved[level_index];

    auto cache_eval = evaluate_cache(
        node, ctx,
        pr.input_hash,
        pr.inputs_frame_dependent,
        pr.has_cacheable_inputs,
        id,
        pr.inputs_all_cache_hits
    );

    if (ctx.diagnostics_enabled) {
        spdlog::debug("[DIAG-exec] frame={} node='{}' id={} kind='{}' cache='{}' frame_dep={} use_cache={} result_ptr={}",
            static_cast<int>(ctx.frame), node.name(), id, to_string(node.kind()),
            cache_eval.cache_status, cache_eval.node_frame_dependent ? 1 : 0,
            cache_eval.use_cache ? 1 : 0,
            cache_eval.result ? fmt::ptr(cache_eval.result.get()) : "null");
    }

    auto predicted_bbox = node.predicted_bbox(ctx, pr.input_bboxes);

    if (ctx.tile_execution_enabled && ctx.active_tile_clip &&
        predicted_bbox && !predicted_bbox->is_empty())
    {
        const auto& tile = *ctx.active_tile_clip;
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

            if (ctx.counters) {
                ctx.counters->nodes_skipped.fetch_add(1, std::memory_order_relaxed);
            }
            return;
        }
    }

    RenderGraphContext node_ctx = ctx;
    if (ctx.dirty_rects_enabled) {
        node_ctx.clip_rect = compute_dirty_clip(ctx, node, predicted_bbox);
    } else {
        node_ctx.clip_rect = predicted_bbox;
    }

    node_ctx.reusable_inputs.clear();
    for (size_t j = 0; j < input_ids.size(); ++j) {
        const GraphNodeId input_id = input_ids[j];
        if (contains_index(state.temp, input_id) && state.temp[input_id]) {
            if (consumer_remaining[input_id].load(std::memory_order_relaxed) == 1 &&
                state.temp[input_id].use_count() == 1) {
                node_ctx.reusable_inputs.push_back(state.temp[input_id].get());
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

    emit_node_records(
        ctx, node,
        cache_eval.key,
        cache_eval.result,
        node_ctx.clip_rect,
        cache_eval.cache_status,
        cache_eval.is_cacheable,
        static_cast<int>(input_ids.size()),
        duration_ms
    );

    state.temp[id] = cache_eval.result;
    state.resolved_key_digest[id] = cache_eval.key.digest();
    state.resolved_frame_dependent[id] = cache_eval.node_frame_dependent ? 1 : 0;
    state.resolved_cache_hit[id] = (cache_eval.cache_status == "hit") ? 1 : 0;
    state.resolved_bboxes[id] = predicted_bbox;
}

} // namespace chronon3d::graph
