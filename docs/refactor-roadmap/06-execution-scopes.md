# Work Package 6 — Execution scopes

## Goal

Use explicit root, tile, and precomp scopes instead of arena override parameters.

## TODO

### PR 6.0
- [ ] Define root, tile, and precomp scope kinds.
- [ ] Define `ExecutionScope` with session, arena, graph ID, and parent relation.

### PR 6.1
- [ ] Change executor input to `ExecutionScope&` plus `ExecutionScheduler&`.
- [ ] Read temporary memory only from the scope.

### PR 6.2
- [ ] Create a root scope in the render pipeline.
- [ ] Reset only root temporary memory when it ends.

### PR 6.3
- [ ] Create one tile scope per coalesced region.
- [ ] Give every tile its own arena while keeping the parent session.

### PR 6.4
- [ ] Create a child scope for precomp execution.
- [ ] Give the child a separate arena and graph ID.
- [ ] Ensure child cleanup does not reset parent memory.

### PR 6.5
- [ ] Track active precomp instance keys in the scope.
- [ ] Reject recursive precomposition with a clear error.

### PR 6.6
- [ ] Keep the program lease active for the complete child execution.
- [ ] Verify tile plus precomp execution cannot refresh one program concurrently.

### PR 6.7
- [ ] Test root, tile, and child memory isolation.
- [ ] Test precomp inside tile execution.
- [ ] Test recursive precomp rejection.
- [ ] Run sanitizer validation for teardown paths.

### PR 6.8
- [ ] Add checks preventing arena override parameters from returning.

## Exit criteria

- [ ] Executor receives an execution scope and scheduler.
- [ ] Root, tile, and child lifetimes are independent.
- [ ] Nested execution is recursion-safe and tile-safe.
