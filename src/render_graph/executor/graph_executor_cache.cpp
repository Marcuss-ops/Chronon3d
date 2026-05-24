#include "graph_executor_internal.hpp"
#include <chronon3d/cache/disk_node_cache.hpp>
#include <chronon3d/cache/persistent_bake_cache.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::graph {

PreResolvedNode resolve_inputs(
    const RenderGraph& graph,
    GraphNodeId id,
    ExecutionState& state,
    const std::pmr::vector<std::atomic_size_t>& consumer_remaining,
    std::pmr::memory_resource* res
) {
    const auto& input_ids = graph.inputs(id);
    PreResolvedNode pr(res);
    pr.inputs.resize(input_ids.size());
    pr.input_bboxes.resize(input_ids.size());
    pr.inputs_all_cache_hits = !input_ids.empty();

    for (size_t j = 0; j < input_ids.size(); ++j) {
        const GraphNodeId input_id = input_ids[j];
        if (contains_index(state.temp, input_id)) {
            const bool is_last_consumer =
                contains_index(consumer_remaining, input_id) &&
                consumer_remaining[input_id].load(std::memory_order_relaxed) == 1;
            const bool duplicated_input =
                std::count(input_ids.begin(), input_ids.end(), input_id) > 1;

            if (is_last_consumer && !duplicated_input && !state.resolved_cache_hit[input_id]) {
                pr.inputs[j] = std::move(state.temp[input_id]);
            } else {
                pr.inputs[j] = state.temp[input_id];
            }
        }
        if (contains_index(state.resolved_bboxes, input_id)) {
            pr.input_bboxes[j] = state.resolved_bboxes[input_id];
        }
        if (contains_index(state.resolved_frame_dependent, input_id)) {
            pr.inputs_frame_dependent |= (state.resolved_frame_dependent[input_id] != 0);
            pr.has_cacheable_inputs = true;
        }
        if (contains_index(state.resolved_cache_hit, input_id)) {
            pr.inputs_all_cache_hits &= (state.resolved_cache_hit[input_id] != 0);
        } else {
            pr.inputs_all_cache_hits = false;
        }
        if (contains_index(state.resolved_key_digest, input_id)) {
            pr.input_hash = hash_combine(pr.input_hash, state.resolved_key_digest[input_id]);
        }
    }
    return pr;
}

CacheEvalResult evaluate_cache(
    const RenderGraphNode& node,
    const RenderGraphContext& ctx,
    u64 input_hash,
    bool inputs_frame_dependent,
    bool has_cacheable_inputs,
    GraphNodeId node_id,
    bool inputs_all_cache_hits
) {
    CacheEvalResult cr;
    const auto policy = node.cache_policy();
    bool is_cacheable = policy.cacheable;

    cr.node_frame_dependent =
        policy.frame_dependent ||
        (has_cacheable_inputs && inputs_frame_dependent);

    if (node.kind() == RenderGraphNodeKind::Composite && inputs_all_cache_hits) {
        is_cacheable = true;
        cr.node_frame_dependent = false;
    }

    cr.use_cache = is_cacheable && ctx.node_cache && !cr.node_frame_dependent;
    cr.is_cacheable = is_cacheable;

    if (cr.use_cache) {
        cr.key = node.cache_key(ctx);
        cr.key.input_hash = input_hash;

        cr.result = ctx.node_cache->get(cr.key);
        
        if (!cr.result && policy.disk_cacheable) {
            // Try persistent bake cache first (faster raw binary format)
            cr.result = cache::PersistentBakeCache::instance().load(cr.key);
            if (!cr.result) {
                // Fall back to legacy disk cache (CFB1 format)
                cr.result = cache::DiskNodeCache::instance().get(cr.key);
            }
            if (cr.result) {
                // Warm up RAM cache
                ctx.node_cache->store(cr.key, cr.result);
            }
        }

        if (ctx.counters) {
            if (cr.result) {
                ctx.counters->cache_hits.fetch_add(1, std::memory_order_relaxed);
                cr.cache_status = "hit";
            } else {
                ctx.counters->cache_misses.fetch_add(1, std::memory_order_relaxed);
                cr.cache_status = "miss";
            }
        } else {
            cr.cache_status = cr.result ? "hit" : "miss";
        }
    } else {
        if (!ctx.node_cache) {
            cr.cache_status = "bypass_no_cache";
        } else if (!is_cacheable) {
            cr.cache_status = "bypass_not_cacheable";
            if (ctx.counters) {
                ctx.counters->bypass_not_cacheable_count.fetch_add(1, std::memory_order_relaxed);
            }
            if (ctx.diagnostics_enabled) {
                spdlog::warn("[cache-bypass] frame={} node='{}' node_id={} kind='{}' reason='not_cacheable'",
                             static_cast<int>(ctx.frame), node.name(), node_id, to_string(node.kind()));
            }
        } else {
            cr.cache_status = "bypass_frame_dependent";
            if (ctx.diagnostics_enabled) {
                spdlog::debug("[cache-bypass] frame={} node='{}' node_id={} kind='{}' reason='frame_dependent'",
                              static_cast<int>(ctx.frame), node.name(), node_id, to_string(node.kind()));
            }
        }
    }
    return cr;
}

} // namespace chronon3d::graph
