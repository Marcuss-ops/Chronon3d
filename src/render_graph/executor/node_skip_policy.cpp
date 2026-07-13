// ============================================================================
// node_skip_policy.cpp — P1 §5 unified skip-policy implementation.
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
    std::string_view node_name)
{
    // ── Acquire + clear transparent 64×64 ────────────────────────────────
    // Preserva esattamente lo stesso path di entrambi i blocchi originali:
    //   (a) early_exit_skip:  acquire_owned_fb(64, 64, false); clear(transparent)
    //   (b) opacity-threshold: acquire_owned_fb(64, 64, false); clear(transparent)
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

    // ── Reset 4 slot resolved_* per il nodo skipped ─────────────────────
    // Identico a entrambi i blocchi originali (comment "state.resolved_* = 0"
    // ripetuto 2 volte; preservato come operazione atomica).
    state.resolved_key_digest[id] = 0;
    state.resolved_frame_dependent[id] = 0;
    state.resolved_cache_hit[id] = 0;
    state.resolved_bboxes[id] = raster::BBox{0, 0, 0, 0};

    // ── Counter bump ────────────────────────────────────────────────────
    // Entrambi i blocchi originali bumpano `layers_culled` (1x).  Only
    // early_exit_skip + Clear node aggiunge i counter Clear-specific.
    if (ctx.node_exec.counters) {
        ctx.node_exec.counters->layers_culled.fetch_add(1, std::memory_order_relaxed);
        if (reason == SkipReason::EarlyExit && node_name == "Clear") {
            ctx.node_exec.counters->clear_skipped_calls.fetch_add(1, std::memory_order_relaxed);
            const uint64_t clear_pixels = static_cast<uint64_t>(ctx.frame_input.width) * static_cast<uint64_t>(ctx.frame_input.height);
            ctx.node_exec.counters->clear_skipped_pixels.fetch_add(clear_pixels, std::memory_order_relaxed);
        }
    }
}

} // namespace chronon3d::graph
