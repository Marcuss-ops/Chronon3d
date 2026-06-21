#pragma once

#include <chronon3d/render_graph/core/node_identity.hpp>

namespace chronon3d::graph {

struct FrameGraphCompileOptions {
    bool run_optimizer{true};
    bool compute_lifetimes{true};
    bool compute_bboxes{true};
    bool validate_dag{true};
    bool include_diagnostics{false};

    // ── WP 4.2 — parent-scope hooks ──────────────────────────────────
    // When this compiler invocation is producing a NESTED compiled
    // graph (e.g. a Precomp layer), the parent graph identity +
    // parent's own stable node are folded into the resulting
    // `compiled.graph_instance_id`.  Two sibling Precomp nodes that
    // USE THE SAME composition therefore receive distinct
    // `graph_instance_id` values, which is what prevents their
    // SceneProgramStore buckets from aliasing into a single partition
    // (PR 4.2 + 5.1 — precomp sibling isolation invariant).
    //
    // Both default to the invalid sentinel, which keeps the legacy
    // top-level behavior (graph_instance_id derived from the sorted
    // reachable stable_node_id set alone).  PrecompNode or its
    // driver fills these when invoking the compiler for a nested
    // graph; tests can leave them default.
    GraphInstanceId parent_graph_instance{kInvalidGraphInstanceId};
    StableNodeId    parent_precomp_node{kInvalidStableNodeId};
};

} // namespace chronon3d::graph
