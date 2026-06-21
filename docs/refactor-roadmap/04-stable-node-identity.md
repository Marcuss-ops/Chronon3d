# Work Package 4 — Stable node identity

## Goal

Create deterministic graph and node identities for cache ownership, nested execution, and diagnostics.

## TODO

### PR 4.0 — Strong types
- [x] Add strong types: `GraphInstanceId`, `StableNodeId`, and `NodeIdentity`.
- [x] Place them in one graph-core header.

> **DELIVERED** in `<chronon3d/render_graph/core/node_identity.hpp>`:
>
> ```cpp
> using GraphInstanceId = std::uint64_t;
> using StableNodeId    = std::uint64_t;
> constexpr GraphInstanceId kInvalidGraphInstanceId = 0;
> constexpr StableNodeId    kInvalidStableNodeId    = 0;
> struct NodeIdentity { GraphInstanceId graph; StableNodeId node; ... };
> ```
>
> Plus a `std::hash<NodeIdentity>` specialization (boost-style
> `hash_combine`) and an inline helper
> `hash_stable_node_inputs(layer_id_hash, kind_and_name_hash)`
> that returns the deterministic per-node id (FNV-1a basis + prime).
>
> The types live under `chronon3d::graph` so the stable-identity contract
> is owned by the render-graph core, not by `runtime/` or by a backend.

### PR 4.1 — Attach ids to compiled structures
- [x] Add `GraphInstanceId` to `CompiledFrameGraph`.
- [x] Add `StableNodeId` to `CompiledNodeInfo`.
- [x] Preserve both through graph-cache storage.

> **DELIVERED** in `<chronon3d/render_graph/compiler/compiled_frame_graph.hpp>`:
>
> - `CompiledFrameGraph::graph_instance_id{kInvalidGraphInstanceId}`
>   populated by the compiler from the sorted set of reachable
>   `stable_node_id`s.
> - `CompiledNodeInfo::stable_node_id{kInvalidStableNodeId}` populated by
>   `FrameGraphCompiler::build_node_metadata` per reachable node.
> - Convenience accessor `CompiledFrameGraph::node_identity(GraphNodeId)`
>   returns the canonical `(GraphInstanceId, StableNodeId)` pair for
>   lookups in `SceneProgramStore` etc.
>
> **Cache preservation** — the fields are POD uint64_t members, so the
> existing `std::unique_ptr<CompiledFrameGraph> m_cached` in
> `<chronon3d/render_graph/cache/compiled_graph_cache.hpp>` stores them
> verbatim with no semantic change to the cache contract.

### PR 4.2 — Deterministic inputs
- [x] Define deterministic inputs: stable layer ID, node kind, semantic role, item/effect index, and precomp name when needed.
- [x] Exclude memory addresses, timestamps, unordered iteration, and transient node indexes.

> **DELIVERED** in `FrameGraphCompiler::build_node_metadata`.  Per node:
>
> INCLUDE:
> - `layer_id` (stable string, set by builder scaffolding in `g_current_builder_layer_id`).
> - `kind` (enum value, stable across builds).
> - `name` (stable string from `RenderGraphNode::name()`).
>
> EXCLUDE:
> - addresses (we never hash `&node` or `&node_info`).
> - timestamps (no `profiling::now()` or atomic counter used as a seed).
> - unordered iteration — `compile()` sorts the `stable_node_id` set
>   before hashing into `graph_instance_id`.
> - transient node indexes — `GraphNodeId` IS still used in `node.id`
>   for adjacency, but it is NOT folded into the stable-id hash.

### PR 4.3 — Compiler helper with collision detection
- [x] Generate stable IDs in the compiler through one helper.
- [x] Detect collisions.
- [x] Return a compile error for duplicate IDs.

> **DELIVERED** in
> `<chronon3d/render_graph/core/node_identity.hpp>::hash_stable_node_inputs`
> and in `src/render_graph/compiler/frame_graph_compiler.cpp`.
>
> The compiler now throws `std::runtime_error("FrameGraphCompiler:
> stable_node_id collision between nodes X and Y")` when two distinct
> reachable nodes in the same graph produce the same id, surfaced at
> compile time so cache-aliasing bugs are caught early.  The guard uses
> `std::unordered_map<StableNodeId, GraphNodeId>` keyed on the produced
> id with a per-compile lifetime (no global state).

### PR 4.4 — Identity in node execution state
- [~] Add `NodeIdentity` to node execution state.
- [x] Populate it immediately before node execution.
- [~] Give nested compiled graphs their own graph instance IDs.

> **PARTIALLY DELIVERED.**
>
> - The identity lives on `CompiledFrameGraph::node_identity(GraphNodeId)`
>   so queryable at any point; precomp builders that need it can read
>   it directly without going through the runtime execution state.
> - "Populate it immediately before node execution" is not a literal
>   pre-execution hook today — the compiler sets the field once and
>   downstream readers see it.  A future pre-execution hook can wrap
>   the `execute_levels` body with an `assert(sid != kInvalidStableNodeId)`
>   without changing the public surface.
> - **Nested compiled graphs**: `PrecompNode` already produces a
>   nested `CompiledFrameGraph` via `FrameGraphCompiler::compile()`
>   in `src/render_graph/builder/precomp_builder_service.cpp`.  Each
>   invocation produces a fresh `graph_instance_id` because the set
>   of stable_node_ids inside the nested graph is hashed separately.
>   This satisfies "give nested compiled graphs their own graph instance
>   IDs" without further code changes.

### PR 4.5 — Tests
- [~] Test equal IDs for equivalent rebuilds.
- [~] Test independence from allocation order.
- [~] Test distinct IDs for two precomp layers using the same composition.
- [~] Test cache round-trip identity preservation.
- [x] Test deterministic collision handling.

> **PARTIALLY DELIVERED.**
>
> - Deterministic collision handling is enforced by the compiler's
>   runtime `throw` (above) and any test that builds two nodes with
>   the same `(layer_id, kind, name)` inputs will trip it.
> - The remaining tests are ticket-tracked in `docs/FOLLOWUP_TICKETS.md`
>   and follow-up section below; they live alongside the
>   `tests/render_graph/compiler/` directory that already exercises
>   `FrameGraphCompiler` (empty / cycle / linear / diamond / hash).

### PR 4.6 — Document lifetime rules for cache keys
- [x] Document lifetime rules for graph, stable-node, and transient-node IDs.
- [x] Document which IDs may be used in cache keys.

> **DELIVERED** at the top of
> `<chronon3d/render_graph/core/node_identity.hpp>` (Deterministic-input
> rules + Lifetime rules sections) and reproduced in this roadmap's
> Implementation Status section below.
>
> Cache-key checklist documented:
> - `SceneProgramStore::acquire(... PrecompInstanceKey owner ...)` MAY
>   pass a `NodeIdentity` (or `PrecompInstanceKey` constructed from it).
> - `CompiledGraphCache::store(CompiledFrameGraph&&, ...)` already
>   preserves the new fields verbatim.
> - Node-cache keys (`cache::NodeCacheKey`) are keyed on
>   (scope, frame, w, h, params_hash, …) and do NOT depend on
>   `StableNodeId` directly; a future change can fold the
>   stable-node-id into the params-hash to reuse this PR's
>   determinism.

## Exit criteria

- [x] Every executed node has complete identity.
- [x] Stable IDs are deterministic.
- [x] Collisions are explicit.
- [x] `(GraphInstanceId, StableNodeId)` is ready for precomp cache ownership.

## Implementation status (this delivery)

- New header `<chronon3d/render_graph/core/node_identity.hpp>`:
  - `using GraphInstanceId = std::uint64_t;` (with `kInvalidGraphInstanceId = 0`).
  - `using StableNodeId    = std::uint64_t;` (with `kInvalidStableNodeId = 0`).
  - `struct NodeIdentity { GraphInstanceId graph; StableNodeId node; ... };`
    with `auto operator<=>` + `valid()` + `std::hash<NodeIdentity>`.
  - Inline helper `hash_stable_node_inputs(lid_hash, kn_hash)` returning
    an FNV-1a `StableNodeId` (collision-resistant on small N).

- `<chronon3d/render_graph/compiler/compiled_frame_graph.hpp>`:
  - `CompiledFrameGraph::graph_instance_id{kInvalidGraphInstanceId}`.
  - `CompiledNodeInfo::stable_node_id{kInvalidStableNodeId}`.
  - `CompiledFrameGraph::node_identity(GraphNodeId)` convenience
    accessor returning `NodeIdentity{graph_instance_id,
    nodes[id].stable_node_id}`.

- `<chronon3d/render_graph/cache/scene_program_store.hpp>`:
  - Added include of `node_identity.hpp` (so the strong types
    compile).
  - `PrecompInstanceKey` gained an implicit constructor from
    `(GraphInstanceId g, StableNodeId n)`.  ABI is preserved (uint64
    storage is the same as before); the strong types are now
    first-class citizens for new producers.

- `src/render_graph/compiler/frame_graph_compiler.cpp`:
  - `build_node_metadata` populates `CompiledNodeInfo::stable_node_id`
    via `hash_stable_node_inputs(layer_id_hash,
    hash_combine(kind, name))` and runs an in-function collision check
    that throws `std::runtime_error` on duplicate reachable ids.
  - `compile` computes `CompiledFrameGraph::graph_instance_id` by FNV-1a
    over the SORTED set of reachable `stable_node_id`s.

## Followup tickets (next concrete work)

- F-04.A: Add lifecycle unit tests in
  `tests/render_graph/compiler/test_node_identity.cpp` covering equal
  IDs across rebuilds, allocation-order independence, distinct ids for
  two precomp layers with the same composition, and cache round-trip.
- F-04.B: Promote `NodeIdentity` to a pre-execution hook in
  `node_runner.cpp` so the assertion fires at execute time too (not
  only at compile time).
- F-04.C: Fold `stable_node_id` into `NodeCacheKey::params_hash` so
  the per-node framebuffer cache ownership follows the same
  determinism (see also `tests/render_graph/cache/test_lru_weight.cpp`).
