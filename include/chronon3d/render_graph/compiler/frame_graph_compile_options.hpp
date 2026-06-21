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

    // ── TICKET-008 / §9.4 — `compile_with_reuse` skip predicate ───────────
    // Returns true iff the call is even eligible for the structural-
    // reuse fast path.  Today the only transform `compile()` runs that
    // is NOT part of the skip-payload is the optimizer, so the
    // predicate is gated on `run_optimizer == false`; if the optimizer
    // was enabled for `prior_compiled`, this overload cannot prove
    // whether the optimizer's effects were applied to the prior and
    // therefore cannot safely reuse it.  Future PRs may extend the
    // predicate to include the `run_optimizer=true` case by hashing
    // the optimizer's identity into `prior_compiled`'s payload; that
    // is OUT OF SCOPE for TICKET-008.
    [[nodiscard]] bool reuse_if_unchanged_predicate_safe() const noexcept {
        return !run_optimizer;
    }
};

} // namespace chronon3d::graph
