# Work Package 4 — Remaining stable identity work

## Current state

Compiled graphs and nodes already carry graph and stable-node IDs, and the compiler detects collisions.

## TODO

### PR 4.0 — Replace aliases with real strong types

Current code uses `using ... = std::uint64_t`.

- [ ] Replace `GraphInstanceId` with a wrapper struct.
- [ ] Replace `StableNodeId` with a wrapper struct.
- [ ] Keep explicit `.value` access and comparison operators.
- [ ] Add hash specializations for the wrappers and `NodeIdentity`.
- [ ] Prevent accidental positional swapping at compile time.

### PR 4.1 — Use a deterministic project hash

- [ ] Stop using `std::hash<std::string>` for persistent stable identity.
- [ ] Use one explicitly specified hash algorithm and byte encoding.
- [ ] Include stable layer ID, node kind, semantic role, and item/effect index where required.
- [ ] Include precomp composition identity without making composition name the whole owner key.
- [ ] Add golden-value tests so toolchain changes cannot alter IDs silently.

### PR 4.2 — Fix graph-instance semantics

Current graph ID is derived only from the sorted stable-node set, which can alias two occurrences of the same nested composition.

- [ ] Define whether graph identity means structural graph, compiled instance, or execution owner scope.
- [ ] For precomp cache ownership, include the parent graph scope and parent precomp stable node.
- [ ] Ensure two layers using the same composition receive distinct owner identities.
- [ ] Preserve identity across cache reuse for the same compiled owner.
- [ ] Document lifetime and serialization rules.

### PR 4.3 — Put identity into node execution state

- [ ] Add `NodeIdentity` to `NodeExecutionContext` or its replacement.
- [ ] Populate it immediately before each node executes.
- [ ] Assert that reachable compiled nodes have valid identity.
- [ ] Propagate the correct child graph identity into nested execution.
- [ ] Remove precomp key generation based on `hash_string(comp_name)` and node zero.

### PR 4.4 — Add lifecycle tests

- [ ] Equivalent rebuilds produce equal stable node IDs.
- [ ] Allocation and insertion order do not change stable node IDs.
- [ ] Two precomp layers using the same composition have distinct owner keys.
- [ ] Cache round-trip preserves graph and node identity.
- [ ] Nested compiled graphs receive the intended owner identity.
- [ ] Collision errors remain deterministic.

## Exit criteria

- [ ] Graph and node IDs are true strong types.
- [ ] Persistent IDs do not depend on `std::hash` implementation details.
- [ ] Every executing node receives a valid `NodeIdentity`.
- [ ] Precomp ownership uses parent graph plus stable node identity.
- [ ] Same-composition sibling precomps cannot alias.
