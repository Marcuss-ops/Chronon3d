#include "execution_state.hpp"
#include <chronon3d/cache/persistent_framebuffer_store.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <string_view>

namespace chronon3d::graph {

bool persistent_framebuffer_cache_enabled_for_current_run() {
    return cache::PersistentFramebufferStore::enabled_for_current_run();
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
    bool is_cacheable = policy.enabled();

    const auto t_cache0 = profiling::now();

    cr.node_frame_dependent =
        policy.is_frame_variant() ||
        (has_cacheable_inputs && inputs_frame_dependent);

    if (node.kind() == RenderGraphNodeKind::Composite && inputs_all_cache_hits) {
        is_cacheable = true;
        cr.node_frame_dependent = false;
    }

    cr.use_cache = is_cacheable && ctx.resources.node_cache && !cr.node_frame_dependent;
    cr.is_cacheable = is_cacheable;

    // Always compute the key to ensure we have a valid key digest for telemetry,
    // bypassed nodes, and the downstream video conversion frame cache.
    cr.key = node.cache_key(ctx);

    // Propagate the central sub-frame tick so that every node that generates
    // a frame-dependent cache key gets the same quantised time anchor without
    // having to remember to include it themselves.  Static nodes keep tick=0,
    // avoiding cache pollution.
    if (cr.node_frame_dependent) {
        cr.key.temporal_key = ctx.frame.temporal_key;
    }

    cr.key.input_hash = input_hash;
    if (ctx.tile.tile_execution_enabled && ctx.tile.active_tile_clip) {
        cr.key.tile_x = ctx.tile.active_tile_clip->x0;
        cr.key.tile_y = ctx.tile.active_tile_clip->y0;
        cr.key.tile_size = ctx.tile.tile_size > 0 ? ctx.tile.tile_size : 0;
    }

    if (cr.use_cache) {
        cr.result = ctx.resources.node_cache->get(cr.key);
        
        if (!cr.result && policy.persistent() && persistent_framebuffer_cache_enabled_for_current_run()) {
            cr.result = cache::PersistentFramebufferStore::instance().get(cr.key);
            if (cr.result) {
                ctx.resources.node_cache->store(cr.key, cr.result);
            }
        }

        if (ctx.telemetry.counters) {
            if (cr.result) {
                ctx.telemetry.counters->cache_hits.fetch_add(1, std::memory_order_relaxed);
                cr.cache_status = "hit";
            } else {
                ctx.telemetry.counters->cache_misses.fetch_add(1, std::memory_order_relaxed);
                cr.cache_status = "miss";
            }
        } else {
            cr.cache_status = cr.result ? "hit" : "miss";
        }
    } else {
        if (!ctx.resources.node_cache) {
            cr.cache_status = "bypass_no_cache";
        } else if (!is_cacheable) {
            cr.cache_status = "bypass_not_cacheable";
            if (ctx.telemetry.counters) {
                ctx.telemetry.counters->bypass_not_cacheable_count.fetch_add(1, std::memory_order_relaxed);
            }
            if (ctx.options.diagnostics_enabled) {
                spdlog::warn("[cache-bypass] frame={} node='{}' node_id={} kind='{}' reason='not_cacheable'",
                             static_cast<int>(ctx.frame.frame), node.name(), node_id, to_string(node.kind()));
            }
        } else {
            cr.cache_status = "bypass_frame_dependent";
            if (ctx.options.diagnostics_enabled) {
                spdlog::debug("[cache-bypass] frame={} node='{}' node_id={} kind='{}' reason='frame_dependent'",
                              static_cast<int>(ctx.frame.frame), node.name(), node_id, to_string(node.kind()));
            }
        }
    }
    if (ctx.telemetry.counters) {
        ctx.telemetry.counters->cache_eval_ms.fetch_add(
            static_cast<uint64_t>(profiling::duration_ms(t_cache0, profiling::now())),
            std::memory_order_relaxed);
    }
    return cr;
}

} // namespace chronon3d::graph
