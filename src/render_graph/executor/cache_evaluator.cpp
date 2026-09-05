#include "execution_state.hpp"
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>
#include <chronon3d/render_graph/nodes/render_graph_node.hpp>

namespace chronon3d::graph {

CacheEvalResult evaluate_cache(
    const RenderGraphNode& node,
    const RenderGraphContext& ctx,
    u64 input_hash,
    bool inputs_frame_dependent,
    bool has_cacheable_inputs,
    GraphNodeId node_id,
    bool cache_key_required
) {
    CacheEvalResult cr;
    const auto policy = node.cache_policy();
    bool is_cacheable = policy.enabled();

    // B (hot-path tax): zero clock reads when no sink consumes the timing.
    // cache_eval_wall_ms is accumulated only under `ctx.node_exec.counters`
    // below, so the production hot path (counters == nullptr) pays no
    // profiling::now() at this scope.
    const bool timing_enabled = ctx.node_exec.counters != nullptr;
    const auto t_cache0 =
        timing_enabled ? profiling::now() : profiling::Clock::time_point{};

    cr.node_frame_dependent =
        policy.frame_dependent() ||
        (has_cacheable_inputs && inputs_frame_dependent);

    // Frame-dependent results are already shared between all consumers of the
    // node through ExecutionState::temp for the current frame. Persisting the
    // same full-frame framebuffer in the inter-frame LRU is therefore wasteful:
    // cache_hit_fast_path intentionally cannot reuse it, while the retained
    // shared_ptr prevents its storage from returning to FramebufferPool. Only
    // frame-invariant results enter the inter-frame cache; this is the semantic
    // boundary between reusable text/raster assets and dynamic projections.
    cr.use_cache = is_cacheable && !cr.node_frame_dependent &&
                   ctx.services.node_cache;
    cr.is_cacheable = is_cacheable;

    // Native video backends attach device surfaces to framebuffers.  Keeping
    // those framebuffers in the inter-frame NodeCache also keeps every
    // FrameTransient Vulkan image alive until the whole job ends, eventually
    // exhausting VRAM.  CPU layout/glyph caches remain active; only the
    // framebuffer cache is disabled for this export path so ownership returns
    // to FramebufferPool at the frame boundary.
    if (ctx.services.backend &&
        ctx.services.backend->supports_native_video_surface()) {
        cr.use_cache = false;
    }

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

    // ── HOT-PATH TAX A: topology-gated key construction ────────────────
    // Historically the key was ALWAYS computed here (even for cache-bypassed
    // nodes) because `commit_node_state` records `cr.key.digest()` into
    // `ExecutionState::resolved_key_digest`, and `resolve_inputs` folds that
    // digest into every consumer's `input_hash`.  A bypassed producer whose
    // key were skipped would publish a stale/missing digest, collapsing the
    // consumers' cache keys across frames (NODE-CACHE-KEY-COLLAPSE-ROT).
    //
    // The skip below is therefore NOT a per-node local decision: it is gated
    // on the compiled consumer table.  `CompiledNodeInfo::cache_key_required`
    // is true for every node that can perform a lookup itself or transitively
    // feeds a node that can — so when it is false, NO executed consumer will
    // ever fold this node's digest into a live cache key, and publishing the
    // default-key constant digest is unobservable.  Telemetry consumption is
    // OR-ed in here because the emitter (telemetry_emitter.cpp) reads key
    // fields for every cacheable node in SQLite-enabled builds; this TU sees
    // the same compile-time flag the emitter sees.
#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
    const bool telemetry_consumes_key = is_cacheable && ctx.services.node_cache;
#else
    const bool telemetry_consumes_key = false;
#endif
    const bool may_need_key = cache_key_required || telemetry_consumes_key;
    if (!may_need_key) {
        // No lookup path and no telemetry sink can read this key: skip
        // construction (and any lookup that would need it).
        cr.use_cache = false;
    } else {
        cr.key = node.cache_key(ctx);

        // Propagate the central sub-frame tick so that every node that
        // generates a frame-dependent cache key gets the same quantised time
        // anchor without having to remember to include it themselves.  Static
        // nodes keep tick=0, avoiding cache pollution.
        if (cr.node_frame_dependent) {
            cr.key.temporal_key = ctx.frame_input.temporal_key;
        }

        cr.key.input_hash = input_hash;
        if (ctx.policy.tile_execution_enabled && ctx.node_exec.active_tile_clip) {
            cr.key.tile_x = ctx.node_exec.active_tile_clip->x0;
            cr.key.tile_y = ctx.node_exec.active_tile_clip->y0;
            cr.key.tile_size = ctx.policy.tile_size > 0 ? ctx.policy.tile_size : 0;
        }

        // HOT-PATH TAX D — digest computed exactly once per key, before the
        // key reaches any unordered_map or the resolved_key_digest
        // publication.  After this point the key is read-only; digest() is a
        // single load.
        cr.key.finalize_digest();
    }

    if (ctx.policy.diagnostics_enabled) {
        spdlog::info(
            "[node-cache-diagnostic] frame={} node_id={} stable_node_id={} "
            "graph_instance_id={} node_cache_key_digest={} input_hash={} "
            "temporal_frame={} temporal_tick={} temporal_version={} "
            "cache_status=pending node_kind={}",
            static_cast<int>(ctx.frame_input.frame), node_id,
            ctx.node_exec.current_identity.node.value,
            ctx.node_exec.current_identity.graph.value, cr.key.digest(),
            cr.key.input_hash, static_cast<int>(cr.key.temporal_key.frame),
            cr.key.temporal_key.subframe_tick, cr.key.temporal_key.version,
            to_string(node.kind()));
    }

    if (cr.use_cache) {
        const auto lookup_t0 = ctx.node_exec.counters
            ? profiling::now()
            : profiling::Clock::time_point{};
        cr.result = ctx.services.node_cache->get(cr.key);
        if (ctx.node_exec.counters) {
            ctx.node_exec.counters->node_cache_lookup_wall_us.fetch_add(
                static_cast<uint64_t>(profiling::duration_us(lookup_t0, profiling::now())),
                std::memory_order_relaxed);
        }

        if (ctx.node_exec.counters) {
            if (cr.result) {
                ctx.node_exec.counters->cache_hits.fetch_add(1, std::memory_order_relaxed);
                ctx.node_exec.counters->node_cache_hits.fetch_add(1, std::memory_order_relaxed);
                cr.cache_status = "hit";
            } else {
                ctx.node_exec.counters->cache_misses.fetch_add(1, std::memory_order_relaxed);
                ctx.node_exec.counters->node_cache_misses.fetch_add(1, std::memory_order_relaxed);
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
    if (ctx.policy.diagnostics_enabled) {
        spdlog::info(
            "[node-cache-decision] frame={} node_id={} cache_key_digest={} "
            "cache_status={} cache_decision={} use_cache={} frame_dependent={}",
            static_cast<int>(ctx.frame_input.frame), node_id, cr.key.digest(),
            cr.cache_status, cr.cache_status == "hit" || cr.cache_status == "miss"
                ? "cache_lookup" : "cache_bypass",
            cr.use_cache ? 1 : 0, cr.node_frame_dependent ? 1 : 0);
    }

    if (ctx.node_exec.counters) {
        ctx.node_exec.counters->cache_eval_wall_ms.fetch_add(
            static_cast<uint64_t>(profiling::duration_ms(t_cache0, profiling::now())),
            std::memory_order_relaxed);
    }
    return cr;
}

} // namespace chronon3d::graph
