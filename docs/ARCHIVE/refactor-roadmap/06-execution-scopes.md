# Work Package 6 — Execution scopes

## Current state

No explicit execution-scope abstraction exists yet.

`GraphExecutor` still receives `RenderSession&`, allocates from `session.arena()`, and resets that arena when each execute call returns. Nested precomp execution passes the parent session into another executor call, so the child can reset memory still used by the parent. Tile execution avoids sharing by creating local logical sessions, which also separates job-owned state that should remain shared.

## TODO

### PR 6.0 — Introduce the scope model

Add:

```cpp
enum class ExecutionScopeKind { Root, Tile, Precomp };

struct ExecutionScope {
    RenderSession& session;
    FrameArena& arena;
    GraphInstanceId graph_id;
    ExecutionScope* parent;
    ExecutionScopeKind kind;
};
```

Actions:

- [ ] Define construction and teardown invariants.
- [ ] Make the active arena explicit.
- [ ] Keep scheduler ownership outside the scope.
- [ ] Carry parent relation and graph identity.
- [ ] Keep job-owned store/history on the session, not duplicated in child scopes.

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

- [ ] Remove direct arena selection from `RenderSession` inside executor code.
- [ ] Allocate all execution state from `scope.arena`.
- [ ] Reset only the arena owned by that scope.
- [ ] Keep `GraphExecutor` stateless.
- [ ] Update all production and test call sites.

### PR 6.2 — Add the root scope

- [ ] Construct one root scope per render invocation.
- [ ] Bind the render job session and compiled graph identity.
- [ ] Keep root execution memory alive through every child invocation.
- [ ] Reset root memory only after final output ownership is safe.

### PR 6.3 — Add the precomp child scope

- [ ] Allocate a distinct child arena.
- [ ] Bind the nested compiled graph identity.
- [ ] Preserve the parent session and job-owned program store.
- [ ] Hold the `ProgramLease` for the complete child scope.
- [ ] Ensure child teardown cannot invalidate parent `ExecutionState`.

### PR 6.4 — Replace tile-local sessions with tile scopes

- [ ] Create one tile arena/scope per coalesced region.
- [ ] Reuse the parent render job session and caches.
- [ ] Preserve independent temporary memory.
- [ ] Keep tile clipping and cache-key isolation.
- [ ] Remove ad-hoc logical session construction from tile orchestration.

### PR 6.5 — Add recursion protection

- [ ] Track active precomp owner keys in the scope chain.
- [ ] Reject direct recursion.
- [ ] Reject indirect recursion across multiple compositions.
- [ ] Return a deterministic engine error or empty result according to policy.
- [ ] Do not use a recursive mutex as recursion handling.

### PR 6.6 — Add memory and race tests

- [ ] child scope does not reset parent arena
- [ ] tile scope does not reset root arena
- [ ] precomp inside tile uses an independent child arena
- [ ] two tiles use independent temporary arenas
- [ ] tile scopes still share job-owned program-store state
- [ ] recursive precomp is rejected
- [ ] same-program execution remains protected by Work Package 5 lease
- [ ] ASAN validation for use-after-reset
- [ ] UBSAN validation for nested teardown

### PR 6.7 — Add permanent guards

- [ ] Prevent arena override parameters from returning.
- [ ] Prevent nested execution from passing the parent arena directly.
- [ ] Prevent tile code from creating replacement render jobs.
- [ ] Require explicit scope and scheduler at every executor call.

## Exit criteria

- [ ] Executor receives `ExecutionScope&` and `ExecutionScheduler&`.
- [ ] Root, tile, and precomp arenas have independent lifetimes.
- [ ] Child execution cannot reset parent memory.
- [ ] Tile execution retains job-scoped caches and services.
- [ ] Recursive precomp is deterministically rejected.
