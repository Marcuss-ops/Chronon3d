# Work Package 4 — Stable node identity

## Goal

Create deterministic graph and node identities for cache ownership, nested execution, and diagnostics.

## TODO

### PR 4.0
- [ ] Add strong types: `GraphInstanceId`, `StableNodeId`, and `NodeIdentity`.
- [ ] Place them in one graph-core header.

### PR 4.1
- [ ] Add `GraphInstanceId` to `CompiledFrameGraph`.
- [ ] Add `StableNodeId` to `CompiledNodeInfo`.
- [ ] Preserve both through graph-cache storage.

### PR 4.2
- [ ] Define deterministic inputs: stable layer ID, node kind, semantic role, item/effect index, and precomp name when needed.
- [ ] Exclude memory addresses, timestamps, unordered iteration, and transient node indexes.

### PR 4.3
- [ ] Generate stable IDs in the compiler through one helper.
- [ ] Detect collisions.
- [ ] Return a compile error for duplicate IDs.

### PR 4.4
- [ ] Add `NodeIdentity` to node execution state.
- [ ] Populate it immediately before node execution.
- [ ] Give nested compiled graphs their own graph instance IDs.

### PR 4.5
- [ ] Test equal IDs for equivalent rebuilds.
- [ ] Test independence from allocation order.
- [ ] Test distinct IDs for two precomp layers using the same composition.
- [ ] Test cache round-trip identity preservation.
- [ ] Test deterministic collision handling.

### PR 4.6
- [ ] Document lifetime rules for graph, stable-node, and transient-node IDs.
- [ ] Document which IDs may be used in cache keys.

## Exit criteria

- [ ] Every executed node has complete identity.
- [ ] Stable IDs are deterministic.
- [ ] Collisions are explicit.
- [ ] `(GraphInstanceId, StableNodeId)` is ready for precomp cache ownership.
