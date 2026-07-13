#include "execution_state.hpp"
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::graph {

// persistent framebuffer cache removed (framebuffer_store → framebuffer_pool)
bool persistent_framebuffer_cache_enabled_for_current_run() {
    return false;
}

CacheEvalResult evaluate_cache(
    const RenderGraphNode& node,
    const RenderGraphContext& ctx,
    u64 input_hash,
    bool inputs_frame_dependent,
    bool has_cacheable_inputs,
    GraphNodeId node_id
) {
    CacheEvalResult cr;
    const auto policy = node.cache_policy();
    bool is_cacheable = policy.enabled();

    const auto t_cache0 = profiling::now();

    cr.node_frame_dependent =
        policy.frame_dependent() ||
        (has_cacheable_inputs && inputs_frame_dependent);

    // FrameVariant nodes are explicitly documented as cacheable within the same
    // render frame ("multiple consumers within the same render frame may still
    // dedupe").  The temporal_key (set below when node_frame_dependent) already
    // isolates cache entries across frames, so intra-frame dedup is safe.
    // The cache_hit_fast_path in node_runner.cpp correctly skips frame_dependent
    // nodes, so they still re-execute each frame.
    cr.use_cache = is_cacheable && ctx.services.node_cache;
    cr.is_cacheable = is_cacheable;

    // ── CompositeNode: always bypass cache (zero-copy enablement) ─────
    // Composites are always frame-dependent in practice — the blend
    // output depends on per-frame layout (clip_rect, bbox, top/bottom
    // origin, blend mode) which changes every frame.  Caching the
    // result would keep `state.temp[composite_id].use_count() > 1`
    // (1 from state.temp + 1 from node_cache), which blocks the
    // zero-copy `reusable_inputs` path in `acquire_owned_fb(const
    // Framebuffer&)`.  Bypass cache for ALL composites so the next
    // composite's input has `use_count == 1` and the 1×1-placeholder
    // swap activates.  Static (Background) nodes remain cached.
    if (node.kind() == RenderGraphNodeKind::Composite) {
        cr.use_cache = false;
    }

    // Always compute the key to ensure we have a valid key digest for telemetry,
    // bypassed nodes, and the downstream video conversion frame cache.
    cr.key = node.cache_key(ctx);

    // Propagate the central sub-frame tick so that every node that generates
    // a frame-dependent cache key gets the same quantised time anchor without
    // having to remember to include it themselves.  Static nodes keep tick=0,
    // avoiding cache pollution.
    if (cr.node_frame_dependent) {
        cr.key.temporal_key = ctx.frame_input.temporal_key;
    }

    cr.key.input_hash = input_hash;
    if (ctx.policy.tile_execution_enabled && ctx.node_exec.active_tile_clip) {
        cr.key.tile_x = ctx.node_exec.active_tile_clip->x0;
        cr.key.tile_y = ctx.node_exec.active_tile_clip->y0;
        cr.key.tile_size = ctx.policy.tile_size > 0 ? ctx.policy.tile_size : 0;
    }

    if (cr.use_cache) {
        cr.result = ctx.services.node_cache->get(cr.key);
        
        // persistent framebuffer cache removed (framebuffer_store → framebuffer_pool)
        (void)policy.persistent();

        if (ctx.node_exec.counters) {
            if (cr.result) {
                ctx.node_exec.counters->cache_hits.fetch_add(1, std::memory_order_relaxed);
                cr.cache_status = "hit";
            } else {
                ctx.node_exec.counters->cache_misses.fetch_add(1, std::memory_order_relaxed);
                cr.cache_status = "miss";
            }
        } else {
            cr.cache_status = cr.result ? "hit" : "miss";
        }
    } else {
        if (!ctx.services.node_cache) {
            cr.cache_status = "bypass_no_cache";
        } else if (node.kind() == RenderGraphNodeKind::Composite) {
            // Explicit composite status — composites always bypass cache so the
            // next composite's input keeps use_count == 1 and the reusable_inputs
            // zero-copy path activates (see cache_skip block above).
            cr.cache_status = "bypass_composite_for_zerocopy";
        } else if (!is_cacheable) {
            cr.cache_status = "bypass_not_cacheable";
            if (ctx.node_exec.counters) {
                ctx.node_exec.counters->bypass_not_cacheable_count.fetch_add(1, std::memory_order_relaxed);
            }
            if (ctx.policy.diagnostics_enabled) {
                spdlog::warn("[cache-bypass] frame={} node='{}' node_id={} kind='{}' reason='not_cacheable'",
                             static_cast<int>(ctx.frame_input.frame), node.name(), node_id, to_string(node.kind()));
            }
        } else {
            cr.cache_status = "bypass_frame_dependent";
            if (ctx.policy.diagnostics_enabled) {
                spdlog::debug("[cache-bypass] frame={} node='{}' node_id={} kind='{}' reason='frame_dependent'",
                              static_cast<int>(ctx.frame_input.frame), node.name(), node_id, to_string(node.kind()));
            }
        }
    }
    if (ctx.node_exec.counters) {
        ctx.node_exec.counters->cache_eval_ms.fetch_add(
            static_cast<uint64_t>(profiling::duration_ms(t_cache0, profiling::now())),
            std::memory_order_relaxed);
    }
    return cr;
}

} // namespace chronon3d::graph
