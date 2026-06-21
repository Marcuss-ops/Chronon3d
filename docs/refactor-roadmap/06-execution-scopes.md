# Work Package 6 — Remaining execution-scope work

## Current problem

Nested precomp execution currently reuses the parent `RenderSession` arena. `GraphExecutor` resets that arena when the child call returns, which can invalidate parent execution state.

Tile execution avoids shared arenas by constructing local sessions, but this loses the intended job-scoped services and is not a general scope model.

## TODO

### PR 6.0 — Introduce explicit scope types

- [ ] Add `ExecutionScopeKind`: root, tile, precomp.
- [ ] Add `ExecutionScope` containing session reference, active arena, graph identity, parent pointer, and scope kind.
- [ ] Make scope construction and teardown RAII-based.
- [ ] Keep the scheduler external and explicit.

### PR 6.1 — Change the executor contract

Target:

```cpp
execute(
    CompiledFrameGraph& graph,
    RenderGraphContext& context,
    ExecutionScope& scope,
    ExecutionScheduler& scheduler
) const;
```

Actions:
- [ ] Remove direct `RenderSession&` arena ownership from executor internals.
- [ ] Obtain the active arena from `ExecutionScope`.
- [ ] Reset only the scope-owned arena at scope completion.
- [ ] Keep `GraphExecutor` stateless.

### PR 6.2 — Add root scope

- [ ] Construct one root scope per render invocation.
- [ ] Bind the job session, root arena, and compiled graph identity.
- [ ] Ensure root temporary memory survives all child calls until root completion.

### PR 6.3 — Replace tile-local sessions with tile scopes

- [ ] Create one tile scope and arena per coalesced dirty region.
- [ ] Keep the parent job services and session-owned caches.
- [ ] Do not create a new logical render job for each tile.
- [ ] Preserve independent temporary memory and current tile clipping.

### PR 6.4 — Add precomp child scope

- [ ] Allocate a distinct child arena for nested execution.
- [ ] Propagate child graph identity and parent relation.
- [ ] Keep parent execution state valid when the child returns.
- [ ] Hold the `ProgramLease` for the entire child scope.

### PR 6.5 — Add recursion protection

- [ ] Track active `PrecompInstanceKey` values in the scope chain.
- [ ] Reject direct and indirect recursive precomposition.
- [ ] Return a clear error or empty result according to the engine error policy.
- [ ] Do not use `recursive_mutex` as recursion handling.

### PR 6.6 — Add memory and race tests

- [ ] child scope does not reset parent arena
- [ ] tile scope does not reset root arena
- [ ] precomp inside tile uses a third independent arena
- [ ] two tiles use independent arenas
- [ ] recursive precomp is rejected
- [ ] same-program tile execution is protected by `ProgramLease`
- [ ] ASAN and UBSAN validation for teardown and nested execution

### PR 6.7 — Add permanent guards

- [ ] Prevent arena override parameters from returning.
- [ ] Prevent nested execution from passing the parent session arena directly.
- [ ] Prevent tile code from creating ad-hoc logical sessions after scope migration.

## Exit criteria

- [ ] Executor receives `ExecutionScope&` and `ExecutionScheduler&`.
- [ ] Root, tile, and child arenas have independent lifetimes.
- [ ] Nested precomp cannot invalidate parent execution memory.
- [ ] Tile execution retains job-scoped services without sharing temporary arenas.
- [ ] Recursive precomp is safely rejected.
