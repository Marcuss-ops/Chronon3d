#include "execution_state.hpp"
#include <chronon3d/cache/disk_node_cache.hpp>
#include <chronon3d/cache/persistent_bake_cache.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <string_view>

namespace chronon3d::graph {

bool disk_node_cache_enabled_for_current_run() {
#ifdef CHRONON_BUILD_TESTS
    return false;
#else
    const char* env = std::getenv("CHRONON_DISABLE_DISK_NODE_CACHE");
    if (!env || !*env) {
        return true;
    }
    const std::string_view value{env};
    return value == "0" || value == "false" || value == "FALSE";
#endif
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

    // Always compute the key to ensure we have a valid key digest for telemetry,
    // bypassed nodes, and the downstream video conversion frame cache.
    cr.key = node.cache_key(ctx);
    cr.key.input_hash = input_hash;
    if (ctx.tile_execution_enabled && ctx.active_tile_clip) {
        cr.key.tile_x = ctx.active_tile_clip->x0;
        cr.key.tile_y = ctx.active_tile_clip->y0;
        cr.key.tile_size = ctx.tile_size > 0 ? ctx.tile_size : 0;
    }

    if (cr.use_cache) {
        cr.result = ctx.node_cache->get(cr.key);
        
        if (!cr.result && policy.disk_cacheable && disk_node_cache_enabled_for_current_run()) {
            cr.result = cache::PersistentBakeCache::instance().load(cr.key);
            if (!cr.result) {
                cr.result = cache::DiskNodeCache::instance().get(cr.key);
            }
            if (cr.result) {
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
