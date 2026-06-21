# Work Package 5 — Remaining SceneProgramStore work

## Current state

Completed:

- `SceneProgramStore` is owned per render session.
- Per-owner policy conflicts are rejected.
- Cache entries use `shared_ptr<CompiledSceneProgram>`.
- In-flight program lifetime survives store `clear()`.
- Automatic tune counting and interval-triggered tuning exist.
- Different owner buckets can be looked up independently without a global render lock.

The current blockers are integration correctness and mutable-program synchronization.

## TODO

### PR 5.0 — Repair `PrecompNode` header/implementation mismatch

Files:
- `include/chronon3d/render_graph/nodes/precomp_node.hpp`
- `src/render_graph/cache/precomp_node_execute.cpp`

Actions:

- [ ] Remove constructor initialization of the deleted `m_instance_key` member.
- [ ] Implement `instance_key(const RenderGraphContext&) const noexcept` with the exact header signature.
- [ ] Use a local owner key derived from `ctx.node_exec.current_identity`.
- [ ] Replace `auto* program = lease.program` with `lease.get()` or an owning local `shared_ptr`.
- [ ] Borrow `session->services.executor`; do not construct `GraphExecutor local_executor`.
- [ ] Validate `ctx.services.scheduler` and executor wiring before nested execution.
- [ ] Update comments that still claim composition-name keying is correct.
- [ ] Add a required compile target/test for this translation unit.

### PR 5.1 — Add a real per-owner execution lease

The current lease protects object lifetime, but not mutable payload refresh and execution.

Required design:

```text
InstanceEntry
  cache
  immutable policy
  execution mutex

ProgramLease
  shared owner entry
  unique execution lock
  shared program
```

Actions:

- [ ] Add one execution mutex per `PrecompInstanceKey` bucket.
- [ ] Acquire the lock before returning a usable program lease.
- [ ] Hold it across payload refresh, parameter refresh, nested execute, and execution accounting.
- [ ] Keep different owner buckets fully concurrent.
- [ ] Do not hold the global store-map mutex during compile or render.
- [ ] Ensure `clear()` cannot invalidate either the program or the locked bucket entry.

### PR 5.2 — Resolve duplicate compile races

`find_or_compile()` currently compiles outside the cache lock, so same-key misses may compile twice.

- [ ] Add per-structure-key single-flight behavior or document and test an accepted duplicate-compile policy.
- [ ] Ensure only one canonical program is inserted for a given owner/structure key.
- [ ] Keep unrelated structure keys concurrent.
- [ ] Count hits/misses consistently under races.

### PR 5.3 — Move mutable parameter state under the same owner

- [ ] Decide whether `FrameParameterBlock` belongs in the per-owner entry.
- [ ] Move it out of `PrecompNode` if it is refreshed with cached program payloads.
- [ ] Protect it with the same execution lease.
- [ ] Prevent sibling or tile executions from mutating one parameter block concurrently.

### PR 5.4 — Fix eviction callback ownership

- [ ] Stop using a raw bucket pointer as the primary callback-rewire identity.
- [ ] Store callback state on the owner entry or return a stable owner generation token.
- [ ] Preserve callback wiring after `clear()` and bucket recreation.
- [ ] Ensure callbacks do not retain a destroyed `PrecompNode` accidentally.

### PR 5.5 — Complete concurrency and lifetime tests

- [ ] repeated-frame reuse for one owner
- [ ] same composition, different owner isolation
- [ ] invalid identity does not alias production buckets
- [ ] conflicting policy rejection
- [ ] automatic tuning interval and min/max behavior
- [ ] reset isolation between sessions
- [ ] concurrent access to one owner bucket
- [ ] parallel access to different owner buckets
- [ ] concurrent `clear()` with an in-flight lease
- [ ] same-key compile race
- [ ] tile refresh plus nested execute on one program
- [ ] eviction callback after bucket recreation

## Dependencies

- Owner-key correctness depends on Work Package 4.
- Full tile/precomp race coverage depends on Work Package 6.

## Exit criteria

- [ ] `PrecompNode` header and implementation compile against one contract.
- [ ] Owner keys come from local node identity.
- [ ] `ProgramLease` protects lifetime and mutable execution.
- [ ] Same-key compilation behavior is deterministic and tested.
- [ ] Mutable parameter state is protected by the owner lease.
- [ ] Same-composition sibling nodes cannot share a bucket accidentally.
