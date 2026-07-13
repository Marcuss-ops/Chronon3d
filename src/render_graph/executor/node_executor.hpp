#pragma once

// ============================================================================
// node_executor.hpp — run_node extraction
//
// P1 step 2 of the multi-stage node_runner.cpp dedup.  Splits:
//   - node.execute() call + NodeExecutionError propagation (P0-1)
//   - OwnedFB→CachedFB conversion (scratch / renderer / pool deleter selection)
//   - node_cache->store(key, result) cache write
// from `node_runner.cpp::execute_single_node` orchestrator into a dedicated
// module.  Zero behavior change (byte-equivalent transcription of the
// original body at lines 21-104 of pre-refactor node_runner.cpp).
//
// CAT-3 minimal-surface:
//   - Zero new singletons, registry, cache, resolver, service locator.
//   - Zero new SDK API (function is `chronon3d::graph`-scoped, internal to
//     the executor module).
//   - No default args (per AGENTS.md `### C++ default-arg uniqueness per TU`).
//   - No `<msdfgen>`, `<libtess2>`, `<unicode[/...]>` includes (Cat-5 fence).
//
// EXCLUDED from this helper (handled by a separate commit per AGENTS.md
// `Fare PR piccole e mirate`):
//   - Dead branch removal at the persistent-cache call site INSIDE run_node.
//     Preserved VERBATIM in node_executor.cpp so that the subsequent
//     `chore(runner): remove dead persistent-cache branch` commit (point 3
//     of the action plan) can purge it without re-touching this extraction.
//
// NAMESPACE: `chronon3d::graph` (matches the surrounding executor code:
// `node_runner.cpp`, `node_skip_policy.hpp`, `node_state_commit.hpp` etc.).
// ============================================================================

#include <chronon3d/internal/render_graph/render_graph.hpp>     // GraphNodeId
#include <chronon3d/core/memory/framebuffer_handle.hpp>        // CachedFB, FramebufferRef, PoolFbDeleter, OwnedFB
#include <chronon3d/cache/node_cache.hpp>                      // NodeCacheKey
#include <chronon3d/math/raster_utils.hpp>                     // raster::BBox full def (std::optional<raster::BBox> in signature — explicit dependency, not transitive-resolved)
#include <chronon3d/render_graph/render_graph_context.hpp>     // RenderGraphContext full def (param)

#include <optional>                                             // std::optional<raster::BBox>
#include <span>                                                 // std::span<const ...>

namespace chronon3d {
    struct RenderCounters;
    namespace cache { class FramebufferPool; }
}

namespace chronon3d::graph {

class RenderGraphNode;       // forward decl: <chronon3d/render_graph/nodes/render_graph_node.hpp>
using FramebufferPool = ::chronon3d::cache::FramebufferPool;

// run_node — execute node and adopt the returned OwnedFB into a CachedFB.
//
// Returns elapsed milliseconds for the execute() call (profiling purpose).
// Public surface for the orchestrator (`node_runner.cpp::execute_single_node`);
// byte-equivalent to the inline body that previously lived at node_runner.cpp:21-104.
//
// Caller-owned `result` parameter is populated with the CachedFB (or remains
// empty on execute-failure).
double run_node(
    RenderGraphNode& node,
    RenderGraphContext& node_ctx,
    std::span<const FramebufferRef> inputs,
    std::span<const std::optional<raster::BBox>> input_bboxes,
    bool use_cache,
    const ::chronon3d::cache::NodeCacheKey& key,
    CachedFB& result,
    const RenderGraphContext& ctx,
    FramebufferPool* parent_pool
);

} // namespace chronon3d::graph
