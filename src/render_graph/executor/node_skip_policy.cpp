// ============================================================================
// node_skip_policy.cpp — P1 §5 unified skip-policy implementation.
// TILE-PRUNE-SKIP-UNIFICATION (2026-07-14): aggiunto SkipReason::TilePruned
// + parametro `bbox_override` opzionale. EarlyExit/OpacityThreshold restano
// byte-equivalent al body unificato P1. TilePruned riusa
// `state.shared_transparent` (no fresh 64×64 alloc) e bump `nodes_skipped`
// (non `layers_culled`); `bbox_override` (default nullopt → BBox{0,0,0,0}
// per retro-compat) si propaga a `state.resolved_bboxes[id]` per preservare
// il `predicted_bbox` del tile-prune branch in node_runner.cpp.
//
// Source-accurate transcription dei due blocchi skip che vivevano inline in
// src/render_graph/executor/node_runner.cpp prima del P1 split.
// Behavior preservation garantita: il body unificato è byte-equivalente
// alla parte comune dei due blocchi originali, con il solo ramo
// Clear-specific isolato dietro SkipReason::EarlyExit + node_name=="Clear".
// ============================================================================

#include "node_skip_policy.hpp"

#include "execution_state.hpp"
#include <chronon3d/core/memory/framebuffer_handle.hpp>   // PoolFbDeleter, ReturnToScratch, RendererOwned, CachedFB
#include <chronon3d/core/memory/framebuffer.hpp>          // Framebuffer full def (used by .release())
#include <chronon3d/core/profiling/counters.hpp>          // RenderCounters (via chronon3d::count)
#include <chronon3d/math/color.hpp>                       // Color::transparent()
#include <chronon3d/render_graph/render_graph_context.hpp> // RenderGraphContext + frame_input access

namespace chronon3d::graph {

void commit_transparent_skip(
    ExecutionState& state,
    GraphNodeId id,
    RenderGraphContext& ctx,
    cache::FramebufferPool* parent_pool,
    SkipReason reason,
    std::string_view node_name,
    std::optional<raster::BBox> bbox_override)
{
    // ── Temp slot acquisition ───────────────────────────────────────────
    // TilePruned: riusa `state.shared_transparent` (no fresh 64×64 alloc,
    // preserva Cat-3 single SSoT + riduce allocazioni).  EarlyExit /
    // OpacityThreshold: acquire_owned_fb(64,64,false) + clear(transparent)
    // preservato byte-equivalent al body P1.
    if (reason == SkipReason::TilePruned) {
        state.temp[id] = state.shared_transparent;
    } else {
        // ── Acquire + clear transparent 64×64 ──────────────────────────
        // Preserva esattamente lo stesso path dei due blocchi originali P1:
        //   early_exit_skip / opacity-threshold: acquire_owned_fb(64,64,false); clear(transparent)
        // `false` = ownership-clear flag non viene impostato sul raw pointer
        // (OwnershipManaged=false), ownership transferred al PoolFbDeleter.
        auto owned_fb = ctx.acquire_owned_fb(64, 64, false);
        owned_fb->clear(Color::transparent());
        Framebuffer* raw = owned_fb.release();
        PoolFbDeleter deleter;
        if (parent_pool) {
            deleter = PoolFbDeleter{parent_pool->shared_from_this()};
        }
        state.temp[id] = CachedFB(raw, std::move(deleter));
    }

    // ── Reset 4 slot resolved_* per il nodo skipped ─────────────────────
    // Identico a entrambi i blocchi originali (comment "state.resolved_* = 0"
    // ripetuto 2 volte; preservato come operazione atomica).  bbox_override
    // (default nullopt → BBox{0,0,0,0}) preserva retro-compat P1 + consente
    // a TilePruned di propagare il `predicted_bbox` del tile-prune branch.
    state.resolved_key_digest[id] = 0;
    state.resolved_frame_dependent[id] = 0;
    state.resolved_cache_hit[id] = 0;
    state.resolved_bboxes[id] = bbox_override.value_or(raster::BBox{0, 0, 0, 0});

    // ── Counter bump ────────────────────────────────────────────────────
    // TilePruned: bump `nodes_skipped` (semantica distinguibile da
    // EarlyExit/OpacityThreshold → `layers_culled`).  EarlyExit + Clear
    // aggiunge i counter Clear-specific (preservato byte-equivalent P1).
    if (ctx.node_exec.counters) {
        if (reason == SkipReason::TilePruned) {
            ctx.node_exec.counters->nodes_skipped.fetch_add(1, std::memory_order_relaxed);
        } else {
            ctx.node_exec.counters->layers_culled.fetch_add(1, std::memory_order_relaxed);
            if (reason == SkipReason::EarlyExit && node_name == "Clear") {
                ctx.node_exec.counters->clear_skipped_calls.fetch_add(1, std::memory_order_relaxed);
                const uint64_t clear_pixels = static_cast<uint64_t>(ctx.frame_input.width) * static_cast<uint64_t>(ctx.frame_input.height);
                ctx.node_exec.counters->clear_skipped_pixels.fetch_add(clear_pixels, std::memory_order_relaxed);
            }
        }
    }
}

} // namespace chronon3d::graph
