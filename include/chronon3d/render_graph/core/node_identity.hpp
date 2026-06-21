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
// WP 4.0 — `GraphInstanceId` and `StableNodeId` are now STRONG WRAPPER
// STRUCTS rather than `using = uint64_t` aliases.  Conversions BETWEEN
// `GraphInstanceId` ↔ `StableNodeId` are NOT defined, so a positional
// swap like `make_precomp_key(StableNodeId{42}, GraphInstanceId{42})`
// fails to compile at the call site.  Each wraps a single
// `std::uint64_t` exposed via `.value`; implicit conversion FROM
// `std::uint64_t` is allowed so aggregate literal init in tests
// (`NodeIdentity{1, 5}`) still compiles while the compiler refuses
// the unsafe swap at the call site of the typed factory.
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

// ── Strong types (WP 4.0) ──────────────────────────────────────────────
//
// Each wraps a single `std::uint64_t`.  No implicit conversion between
// GraphInstanceId ↔ StableNodeId is defined — `make_precomp_key` calls
// where the two strong types are swapped in argument order fail to
// compile at the call site, satisfying the "prevent accidental positional
// swapping at compile time" exit criterion of WP 4.0.
//
// Implicit conversion FROM `std::uint64_t` is preserved so existing
// aggregate literal init sites (`NodeIdentity{1, 5}`,
// `PrecompInstanceKey{17, 23}`) keep building without a tide of churn
// at every call site; the strong types only refuse cross-typing.
struct GraphInstanceId {
    std::uint64_t value{0};
    constexpr GraphInstanceId() noexcept = default;
    constexpr explicit(false) GraphInstanceId(std::uint64_t v) noexcept : value(v) {}
    constexpr auto operator<=>(const GraphInstanceId&) const = default;
    constexpr bool operator==(const GraphInstanceId&) const = default;
};

struct StableNodeId {
    std::uint64_t value{0};
    constexpr StableNodeId() noexcept = default;
    constexpr explicit(false) StableNodeId(std::uint64_t v) noexcept : value(v) {}
    constexpr auto operator<=>(const StableNodeId&) const = default;
    constexpr bool operator==(const StableNodeId&) const = default;
};

// Sentinel constants — defined AFTER the structs so the default member
// initializers on `NodeIdentity` and elsewhere can reference them.
inline constexpr GraphInstanceId kInvalidGraphInstanceId{};
inline constexpr StableNodeId    kInvalidStableNodeId{};

/// (GraphInstanceId, StableNodeId) pair — the canonical identity for a
/// node living inside a specific compiled graph.
///
/// Stays an aggregate (default member initializers present) so call
/// sites that use designated-init (`.graph = …, .node = …`) keep
/// compiling.  The implicit uint64_t ctors on the strong types mean
/// positional aggregate init like `NodeIdentity{42, 99}` also
/// works — the test fixture in tests/render_graph/core/test_node_identity.cpp
/// relies on this.
struct NodeIdentity {
    GraphInstanceId graph{kInvalidGraphInstanceId};
    StableNodeId    node{kInvalidStableNodeId};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return graph != kInvalidGraphInstanceId
            && node != kInvalidStableNodeId;
    }

    constexpr auto operator<=>(const NodeIdentity&) const = default;
    constexpr bool operator==(const NodeIdentity&) const = default;
};

}  // namespace chronon3d::graph

// std::hash specializations.
//
// `std::hash` requires default-constructibility + copy-constructibility
// for use inside `std::unordered_map<K, …>`.  The C++ standard
// library's fallback for struct types is SFINAE-disabled (the
// `__hash_not_enabled` machinery); we MUST provide a complete
// specialization for any strong-type used as an unordered_map key.
//
// Note: do NOT declare a destructor or any non-copyable member on
// these specializations — gcc rejects the unordered_map default ctor
// ("hash function must be copy constructible") if the hash itself is
// not copy-constructible.

// NodeIdentity — `hash_combine`-style merge so that
// `(g1, n1) != (g1, n2)` and `(g1, n1) != (g2, n1)` even when the
// individual hashes collide (FNV-1a enhancement).
template <>
struct std::hash<chronon3d::graph::NodeIdentity> {
    std::size_t operator()(const chronon3d::graph::NodeIdentity& id) const noexcept {
        std::size_t h = std::hash<std::uint64_t>{}(id.graph.value);
        h ^= std::hash<std::uint64_t>{}(id.node.value)
             + 0x9e3779b9ULL
             + (h << 6)
             + (h >> 2);
        return h;
    }
};

// GraphInstanceId — use the wrapped uint64_t hash directly.
template <>
struct std::hash<chronon3d::graph::GraphInstanceId> {
    std::size_t operator()(const chronon3d::graph::GraphInstanceId& id) const noexcept {
        return std::hash<std::uint64_t>{}(id.value);
    }
};

// StableNodeId — use the wrapped uint64_t hash directly.  This is
// the missing specialization that broke
// `std::unordered_map<StableNodeId, …>` in
// src/render_graph/compiler/frame_graph_compiler.cpp:269 (gcc
// "hash function must be copy constructible" — the std fallback
// blocks the SFINAE dance because `StableNodeId`'s default ctor +
// `operator==` are user-declared and the fallback's destructor is
// implicitly deleted).
template <>
struct std::hash<chronon3d::graph::StableNodeId> {
    std::size_t operator()(const chronon3d::graph::StableNodeId& id) const noexcept {
        return std::hash<std::uint64_t>{}(id.value);
    }
};

namespace chronon3d::graph {

/// Helper used by `FrameGraphCompiler` (PR 4.3) to derive a stable
/// per-node id from a node's stable inputs.  The hash starts from the
/// FNV-1a offset basis and mixes two 64-bit lanes (the hash of
/// `layer_id` and the hash of `kind`+`name`) — chosen for collision
/// resistance under small N without dragging in a third-party hash
/// dependency.
///
/// WP 4.1 — the inputs are XXH64 hashes (via
/// `chronon3d::graph::hash_string`) rather than `std::hash<std::string>`,
/// so identical inputs produce identical outputs across compilers /
/// libstdc++ versions — required for cache round-trip safety (PR 4.4).
///
/// Returns a `StableNodeId` strong wrapper so the result plugs into
/// `CompiledNodeInfo::stable_node_id` without further conversion.
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
    return StableNodeId{h == 0u ? 1u : h};
}

}  // namespace chronon3d::graph
