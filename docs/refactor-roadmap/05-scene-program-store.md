# Work Package 5 — Remaining SceneProgramStore work

## Current state

`SceneProgramStore`, `PrecompCachePolicy`, and a thin `PrecompNode` exist. The remaining work is correctness, ownership, identity, and concurrency.

## TODO

### PR 5.0 — Repair current call sites

- [ ] Replace stale `session->program_store->...` calls with the final accessor or job-owned store API.
- [ ] Update precomp tests that still access a removed `program_store` field.
- [ ] Borrow the wired executor service instead of constructing a local executor in `PrecompNode::execute()`.
- [ ] Add a compile-focused test covering the precomp execution translation unit.

### PR 5.1 — Use the real precomp owner key

Current keying uses composition-name hash plus node zero.

- [ ] Build `PrecompInstanceKey` from parent `GraphInstanceId` and current `StableNodeId`.
- [ ] Obtain identity from node execution state, not from `comp_name` alone.
- [ ] Remove the cached composition-name-only key from `PrecompNode`.
- [ ] Test two sibling nodes using the same composition and verify separate buckets.

### PR 5.2 — Implement a real `ProgramLease`

The lease must own lifetime and synchronization, not only a raw pointer.

- [ ] Hold a `shared_ptr` to the selected program or another stable owning handle.
- [ ] Hold a per-instance execution lock across lookup, compile, payload refresh, nested execute, and accounting.
- [ ] Keep `clear()` from invalidating an in-flight program.
- [ ] Allow different precomp instance buckets to execute concurrently.
- [ ] Avoid a global store lock during compilation or rendering.

### PR 5.3 — Make policy immutable per owner

- [ ] Reject conflicting cache policy for an existing owner key.
- [ ] Do not silently reconfigure one bucket because another caller supplied different policy.
- [ ] Record composition identity and policy in the bucket for diagnostics.
- [ ] Forward eviction callbacks without storing mutable callback ownership on the node when avoidable.

### PR 5.4 — Restore automatic tuning

- [ ] Add an atomic execution counter to the cache or bucket.
- [ ] Add `record_execution()` and trigger `auto_tune()` at `TuneConfig::interval`.
- [ ] Count both hits and misses consistently.
- [ ] Preserve min/max capacity and fixed mode.
- [ ] Reset tuning counters when the owning render job resets.
- [ ] Test tuning through `SceneProgramStore`, not by manually calling `cache.auto_tune()`.

### PR 5.5 — Restore job-scoped ownership

- [ ] Move mutable store ownership from `RenderRuntime` to the render job/session.
- [ ] Keep runtime-level defaults or policy factories immutable.
- [ ] Ensure one session reset cannot clear another session's programs.
- [ ] Preserve a graph-free runtime session header using opaque implementation storage if needed.

### PR 5.6 — Resolve mutable parameter state

- [ ] Decide whether `FrameParameterBlock` belongs to the node or the per-instance bucket.
- [ ] Move it into the bucket if it is refreshed together with the cached compiled program.
- [ ] Protect it with the same lease when mutable.

### PR 5.7 — Add concurrency and lifetime tests

- [ ] repeated-frame reuse for one owner
- [ ] same composition, different owner isolation
- [ ] conflicting policy rejection
- [ ] automatic tuning interval
- [ ] reset isolation between sessions
- [ ] concurrent access to one bucket
- [ ] parallel access to different buckets
- [ ] concurrent `clear()` with an in-flight lease
- [ ] tile refresh plus nested execute on the same program

## Exit criteria

- [ ] All call sites use the final store API.
- [ ] Owner keys come from graph and stable node identity.
- [ ] `ProgramLease` protects lifetime and mutable execution.
- [ ] Auto-tune runs automatically.
- [ ] Mutable store state is isolated per render job.
- [ ] Same-composition sibling nodes cannot share a bucket accidentally.
