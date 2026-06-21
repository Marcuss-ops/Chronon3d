#pragma once

// ---------------------------------------------------------------------------
// render_graph/core/node_identity.hpp
//
// Work Package 4 — Stable node identity.
// ---------------------------------------------------------------------------
//
// Three strongly-typed identities form the contract that lets the
// render-graph and the program-cache reason about "which node in
// which graph" without depending on transient pointers, build-time
// hashes, or memory addresses:
//
//   - GraphInstanceId   — identifies one CompiledFrameGraph.  Two
//     structurally-equivalent graphs built from the same source in the
//     same order MUST yield the same id; build / pointer / time inputs
//     MUST NOT influence it.  Nested compiled graphs (precomp layers)
//     each get their OWN graph instance id, even when their structure
//     is identical — this is what lets two precomp layers using the
//     same composition cache independently.
//
//   - StableNodeId      — identifies one CompiledNodeInfo inside a
//     CompiledFrameGraph.  Built from the node's `layer_id`,
//     `RenderGraphNodeKind`, and `name` — never from a pointer, an
//     allocation index, a timestamp, or any pointer-derived hash.
//
//   - NodeIdentity      — `(GraphInstanceId, StableNodeId)` pair.
//     This is the cache-key shape used by `SceneProgramStore` for
//     per-precomp-instance ownership (PR-5 already uses
//     `PrecompInstanceKey`; this is the broader superset that PR 4.1
//     brings onto every compiled node).
//
// Deterministic-input rules (PR 4.2):
//   INCLUDE: layer_id (stable string), kind (enum), name (stable string),
//            semantic role / item_index / effect_index from binding_meta,
//            precomp composition name when present.
//   EXCLUDE: addresses, timestamps, unordered iteration order,
//            transient index counters from non-cyclic builds.
//
// Lifetime rules (PR 4.6):
//   - GraphInstanceId and StableNodeId are content-derived integers.
//     They are valid as long as the topology that produced them is
//     reconstructable from the same source.
//   - They MAY be used as cache keys (SceneProgramStore,
//     CompiledGraphCache, scratch-buffer keys keyed on stable_node_id).
//   - They MUST NOT be used as replacements for the dynamic
//     `GraphNodeId` (which still tracks adjacency edges and
//     `.nodes[*]` indexing for the hot loop).
//
// The collision policy is enforced by the FrameGraphCompiler helper
// (PR 4.3) which throws when two distinct nodes in the same graph
// produce the same `(layer_id, kind, name)` hash.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <functional>

namespace chronon3d::graph {

using GraphInstanceId = std::uint64_t;
using StableNodeId    = std::uint64_t;

constexpr GraphInstanceId kInvalidGraphInstanceId = 0;
constexpr StableNodeId    kInvalidStableNodeId    = 0;

/// (GraphInstanceId, StableNodeId) pair — the canonical identity for a
/// node living inside a specific compiled graph.
struct NodeIdentity {
    GraphInstanceId graph{kInvalidGraphInstanceId};
    StableNodeId    node{kInvalidStableNodeId};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return graph != kInvalidGraphInstanceId
            && node != kInvalidStableNodeId;
    }

    auto operator<=>(const NodeIdentity&) const = default;
};

}  // namespace chronon3d::graph

// std::hash specialization — `hash_combine`-style merge so that
// `(g1, n1) != (g1, n2)` and `(g1, n1) != (g2, n1)` even when the
// individual hashes collide (FNV-1a enhancement).
template <>
struct std::hash<chronon3d::graph::NodeIdentity> {
    std::size_t operator()(const chronon3d::graph::NodeIdentity& id) const noexcept {
        std::size_t h = std::hash<std::uint64_t>{}(id.graph);
        h ^= std::hash<std::uint64_t>{}(id.node)
             + 0x9e3779b9ULL
             + (h << 6)
             + (h >> 2);
        return h;
    }
};

namespace chronon3d::graph {

/// Helper used by `FrameGraphCompiler` (PR 4.3) to derive a stable
/// per-node id from a node's stable inputs.  The hash starts from the
/// FNV-1a offset basis and mixes two 64-bit lanes (the hash of
/// `layer_id` and the hash of `kind`+`name`) — chosen for collision
/// resistance under small N without dragging in a third-party hash
/// dependency.
[[nodiscard]] inline StableNodeId hash_stable_node_inputs(
    std::uint64_t layer_id_hash,
    std::uint64_t kind_and_name_hash
) noexcept {
    // FNV-1a (xorshift then multiply).
    constexpr std::uint64_t kOffset = 0xcbf29ce484222325ULL;
    constexpr std::uint64_t kPrime  = 0x100000001b3ULL;
    std::uint64_t h = kOffset;
    h ^= layer_id_hash;
    h *= kPrime;
    h ^= kind_and_name_hash;
    h *= kPrime;
    // Reserved null-guard: 0 is the invalid sentinel.
    return h == 0 ? 1 : h;
}

}  // namespace chronon3d::graph
