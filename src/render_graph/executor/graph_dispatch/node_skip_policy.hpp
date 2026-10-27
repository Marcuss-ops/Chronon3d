#pragma once

// ============================================================================
// node_skip_policy.hpp — P1 §5 extracted unified skip-policy.
//
// Splittito da src/render_graph/executor/node_runner.cpp (P1 step 1/3).
// Il percorso di skip early-exit e quello di tile-pruning condividono la
// costruzione dello stato trasparente; questo modulo li consolida in un'unica
// funzione `commit_transparent_skip()` parametrizzata da `SkipReason`.

// Perché Enum+Single Function (vs 2 funzioni separate):
//   - Cat-3 anti-duplication: i due blocchi condividono ~90% del codice
//     (acquire_owned_fb/clear/transparent/CachedFB construction/state reset);
//     due funzioni clonerebbero la parte comune.
//   - SkipReason enum rende esplicito il discriminante + consente lookup
//     one-shot per la slice counter-additiva (solo EarlyExit + "Clear" bump
//     `clear_skipped_calls/clear_skipped_pixels`).
// GATING: nessuno (always-compiled).  I skip path sono correctness-critical
// (precedenza su execute body), devono sempre essere disponibili.
// ============================================================================

#include <optional>
#include <string_view>

#include <chronon3d/internal/render_graph/render_graph.hpp>   // GraphNodeId
#include <chronon3d/math/raster_utils.hpp>                    // raster::BBox

namespace chronon3d::graph {

class ExecutionState;
class RenderGraphContext;
using FramebufferPool = ::chronon3d::cache::FramebufferPool;

// SkipReason — discriminante per commit_transparent_skip().
//   EarlyExit        : ctx.node_exec.early_exit_skip[id] driven.
//                      Emette il bump addizionale clear_skipped_calls /
//                      clear_skipped_pixels quando node_name == "Clear".//   TilePruned        : bbox non interseca active_tile_clip

//                      ([TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX](docs/tickets/archive/TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX.md),
//                      TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION lineage).
//                      Reusa state.shared_transparent (no fresh 64×64 alloc) e
//                      preserva il `predicted_bbox` via `bbox_override` override.
//                      Bump `nodes_skipped` (non `layers_culled`) per spec.
enum class SkipReason {
    EarlyExit,
    TilePruned,
    StaticBaked,  // Phase 4: interior static node pre-baked in prepare()
};

// commit_transparent_skip — produce un CachedFB 64×64 fully-transparent
// (EarlyExit) oppure riusa `state.shared_transparent`
// (TilePruned, no fresh alloc), reset dei 4 slot `state.resolved_*` per
// `id`, e bump del counter specifico per reason.  Per TilePruned,
// `bbox_override` viene propagato a `state.resolved_bboxes[id]` (preserva
// `predicted_bbox` dal tile-prune branch di node_runner.cpp).  Per
// SkipReason::EarlyExit + node_name=="Clear", aggiunge anche
// clear_skipped_calls + clear_skipped_pixels.  Da chiamare in
// execute_single_node appena la condizione di skip è verificata + prima
// di qualsiasi altra elaborazione (cache eval, run_node call, ecc.).
//
// `bbox_override` ha default `std::nullopt` → il call site EarlyExit conserva
// il comportamento storico (write `BBox{0,0,0,0}`).
void commit_transparent_skip(
    ExecutionState& state,
    GraphNodeId id,
    RenderGraphContext& ctx,
    FramebufferPool* parent_pool,
    SkipReason reason,
    std::string_view node_name = {},
    std::optional<raster::BBox> bbox_override = std::nullopt
);

} // namespace chronon3d::graph
