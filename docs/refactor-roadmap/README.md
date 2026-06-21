# Chronon3d refactor roadmap — remaining work

This directory contains only unfinished architectural work.

`PR` numbers are implementation steps, not GitHub pull requests.

## Audited main

- Commit: `591f8e1ea0793902684389b97d1e509aae455533`
- Compared with the previous audit: no newer commits
- Remote combined status: no status checks exposed for this commit

## Completed foundations

The active roadmap no longer tracks these completed foundations:

- compiled-graph-only executor contract
- retirement of raw `RenderGraph` executor overloads
- retirement of `ExecutionPlanCache`
- explicit scheduler parameter on graph execution
- tile orchestration through `ExecutionScheduler`
- strong graph/node ID wrapper types
- deterministic project hashing for stable IDs
- per-session `SceneHasher` and `SceneProgramStore` ownership
- canonical `FrameHistory` and `DirtyHistory`
- automatic cache-tune counter and immutable per-owner policy
- typed `AssetResolver` owned by `RenderRuntime`
- migration away from legacy string-only asset resolution helpers

## Immediate repair order

1. [Work Package 0 — Restore trustworthy build gates](00-baseline-and-gates.md)
2. [Work Package 5 — Repair precomp integration and locking](05-scene-program-store.md)
3. [Work Package 4 — Remove identity race and finish owner propagation](04-stable-node-identity.md)
4. [Work Package 6 — Add root, tile, and child execution scopes](06-execution-scopes.md)
5. [Work Package 1 — Repair and complete scheduler determinism tests](01-scheduler-single-authority.md)
6. [Work Package 3 — Preserve session isolation without runtime-to-graph leakage](03-render-session-boundary.md)
7. [Work Package 8 — Remove asset globals and close the SDK facade](08-global-state-and-sdk.md)
8. [Work Package 7 — Complete the standalone software backend](07-software-backend-completion.md)

## Current status

| Package | Status | Current blocker |
|---|---|---|
| 0 — Validation gates | BLOCKED | boundary check 5 cannot fail the script; stale test API is not caught |
| 1 — Scheduler determinism | BLOCKED | registered test includes removed `ExecutionPlanCache` and calls a removed executor overload |
| 3 — Session boundary | IN PROGRESS | job isolation works, but `runtime/render_session.hpp` includes render-graph implementation headers |
| 4 — Stable identity | BLOCKED | parallel workers write shared `ctx.node_exec.current_identity` before cloning |
| 5 — Scene program store | BLOCKED | `PrecompNode` header/implementation mismatch; owner key and execution lock are incomplete |
| 6 — Execution scopes | TODO | nested precomp still resets the parent session arena |
| 7 — Software backend | TODO | `SoftwareRenderer` still implements and inherits `RenderBackend` |
| 8 — Global state and SDK | IN PROGRESS | typed resolver exists, but deep access still routes through active-runtime/process globals |

## P0 repair set

Complete these before adding more architecture:

- Synchronize `precomp_node_execute.cpp` with `precomp_node.hpp`.
- Derive the precomp owner key from `ctx.node_exec.current_identity`.
- Use `lease.get()` or an owning `shared_ptr`, never assign a `shared_ptr` to a raw-pointer variable.
- Borrow `session.services.executor`; do not construct a local executor in `PrecompNode`.
- Fix scheduler determinism tests to compile a `CompiledFrameGraph` and call the current executor API.
- Move `current_identity` assignment onto the per-node cloned context to remove the parallel data race.
- Fix the architecture script so every check participates in the final exit code.
- Remove `noexcept` from `RenderRuntime::backend()` while it can throw.

## Target architecture

```text
RenderRuntime
  engine configuration
  backend
  scheduler
  registries and catalogs
  engine-lifetime caches
  engine-local AssetResolver

RenderSession
  job history
  dirty history
  job-owned graph state behind an opaque boundary

ExecutionScope
  active arena
  graph/node identity
  parent relation
  root, tile, or precomp kind

SceneProgramStore
  owner key = parent GraphInstanceId + current StableNodeId
  per-owner immutable policy
  lifetime lease
  per-owner execution lock

GraphExecutor
  stateless
  compiled graphs only
  explicit scheduler
  explicit execution scope
```

## Rules

- Restore a coherent build before continuing refactors.
- Never mutate a shared `RenderGraphContext` from parallel workers.
- Never key precomp ownership from composition name alone.
- Keep `ProgramLease` alive and locked through refresh plus nested execution.
- Never let child execution reset a parent arena.
- Do not replace explicit asset injection with another global resolver bridge.
- Update this file whenever a package changes state.
