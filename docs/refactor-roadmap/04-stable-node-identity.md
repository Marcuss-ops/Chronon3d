# Work Package 4 — Remaining stable-identity work

## Current state

Completed:

- `GraphInstanceId` and `StableNodeId` are distinct wrapper structs.
- `NodeIdentity` exists with hash support.
- Persistent node hashing uses the project hash path instead of `std::hash<std::string>`.
- The compiler detects duplicate stable-node IDs.
- Parent graph and parent precomp identity can be mixed into nested graph identity.
- `NodeExecutionContext` carries `current_identity`.

The remaining work is safe execution propagation and real precomp integration.

## TODO

### PR 4.0 — Remove the parallel identity data race

Current problem: parallel workers write `ctx.node_exec.current_identity` on the shared context before cloning it.

Actions:

- [ ] Clone the node context first.
- [ ] Assign `node_ctx.node_exec.current_identity` only on the local clone.
- [ ] Never mutate the shared parent context from parallel node workers.
- [ ] Add a TSAN-oriented test or stress test with multiple nodes in one execution level.
- [ ] Verify cache-hit and early-exit paths do not leak the previous node identity.

Target pattern:

```cpp
auto node_ctx = ctx.clone_for_node_execution();
node_ctx.node_exec.current_identity = {
    compiled.graph_instance_id,
    compiled.nodes[id].stable_node_id
};
```

### PR 4.1 — Connect identity to `PrecompNode`

- [ ] Implement `PrecompNode::instance_key(const RenderGraphContext&)` exactly as declared in the header.
- [ ] Build the owner key from `ctx.node_exec.current_identity` through `make_precomp_key()`.
- [ ] Remove composition-name hash plus node-zero key generation.
- [ ] Reject or clearly isolate direct test calls with invalid identity.
- [ ] Do not silently place multiple invalid-identity nodes into one bucket.

### PR 4.2 — Verify nested graph identity propagation

- [ ] Pass parent graph and parent precomp stable-node ID into nested compile options.
- [ ] Ensure two sibling precomp layers using the same composition get distinct owner graph IDs.
- [ ] Preserve the same owner graph ID across cache reuse and payload refresh.
- [ ] Confirm graph identity is not derived from allocation address, time, or unordered iteration.

### PR 4.3 — Complete lifecycle tests

- [ ] Equivalent rebuilds produce equal stable-node IDs.
- [ ] Allocation and insertion order do not change stable-node IDs.
- [ ] Golden values detect accidental hash-algorithm changes.
- [ ] Two same-composition sibling precomps have distinct owner keys.
- [ ] Nested compiled graph identity contains the expected parent scope.
- [ ] Cache round-trip preserves graph and node identity.
- [ ] Parallel node execution always observes its own local identity.

### PR 4.4 — Optional strong-type hardening

The wrappers currently accept implicit construction from `uint64_t`.

- [ ] Decide whether constructors should become explicit.
- [ ] Migrate aggregate test literals if explicit construction is adopted.
- [ ] Keep cross-type swapping impossible at compile time.

## Exit criteria

- [ ] No shared-context identity write occurs inside parallel execution.
- [ ] Every executing node receives its own valid local `NodeIdentity`.
- [ ] Precomp ownership comes from graph plus stable-node identity.
- [ ] Same-composition sibling precomps cannot alias.
- [ ] Lifecycle and concurrency identity tests pass.
