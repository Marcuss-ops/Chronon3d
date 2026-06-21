# Work Package 5 — Scene program store

## Goal

Move precomposition program caching and auto-tune state out of `PrecompNode` into one session-owned store.

## TODO

### PR 5.0
- [ ] Define `PrecompInstanceKey` from `GraphInstanceId` and `StableNodeId`.
- [ ] Define immutable `PrecompCachePolicy`.

### PR 5.1
- [ ] Add `SceneProgramStore` with one bucket per precomp instance.
- [ ] Keep `SceneStructureKey` as the key inside each bucket.
- [ ] Make store lifetime match the render job/session.

### PR 5.2
- [ ] Add `ProgramLease`.
- [ ] Keep the lease alive across lookup, optional compile, payload refresh, nested execution, and execution accounting.
- [ ] Protect mutable compiled-program payload from concurrent tile refreshes.

### PR 5.3
- [ ] Move auto-tune counter and policy into the store/cache bucket.
- [ ] Preserve interval, minimum capacity, maximum capacity, and tune mode.
- [ ] Reject conflicting policy for the same precomp instance key.

### PR 5.4
- [ ] Reduce `PrecompNode` to composition name, frame range, cache frame, and cache policy.
- [ ] Retire node-owned program cache, executor, plan cache, session, and tune counter.
- [ ] Move `FrameParameterBlock` into the store bucket if it is mutable cached-program state.

### PR 5.5
- [ ] Wire the store through the session or a typed session service.
- [ ] Avoid a process-global or runtime-global mutable program cache.
- [ ] Ensure `reset_job()` clears store entries and tune state.

### PR 5.6
- [ ] Test reuse across frames for one node.
- [ ] Test isolation between two nodes using the same composition.
- [ ] Test auto-tune interval and min/max limits.
- [ ] Test reset behavior.
- [ ] Test concurrent access to one bucket.
- [ ] Test parallel access to different buckets.

## Exit criteria

- [ ] `PrecompNode` owns no executor, session, program cache, plan cache, or tune counter.
- [ ] Cache ownership is keyed by graph and stable node identity.
- [ ] Auto-tune survives frame changes and resets with the job.
- [ ] Same-program tile access is race-safe.
