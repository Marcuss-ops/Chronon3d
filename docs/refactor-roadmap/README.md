# Chronon3d refactor roadmap — remaining work

This directory now lists only unfinished architectural work. Completed checklist sections have been removed.

`PR` numbers are implementation steps, not GitHub pull requests.

## Current main

Audited at commit `53de5b2f8ef48f5ebc0f29d2dafe4841a2674f31`.

Completed and removed from the active roadmap:

- compiled-graph-only executor contract
- retirement of raw `RenderGraph` executor overloads
- retirement of `ExecutionPlanCache`
- explicit runtime scheduler parameter on graph execution
- removal of direct TBB orchestration from the tile coordinator
- split of `SoftwareRenderSession` from the runtime header
- retirement of `clear_per_frame()`

The completed Work Package 2 document was removed. Its only optional optimization follow-up remains tracked as `TICKET-008` in `docs/FOLLOWUP_TICKETS.md`.

## Active execution order

1. [Work Package 0 — Repair validation gates](00-baseline-and-gates.md)
2. [Work Package 5 — Make SceneProgramStore correct](05-scene-program-store.md)
3. [Work Package 6 — Add execution scopes](06-execution-scopes.md)
4. [Work Package 4 — Complete stable identity](04-stable-node-identity.md)
5. [Work Package 3 — Finish session isolation](03-render-session-boundary.md)
6. [Work Package 1 — Finish scheduler determinism](01-scheduler-single-authority.md)
7. [Work Package 7 — Complete the software backend](07-software-backend-completion.md)
8. [Work Package 8 — Remove globals and close the SDK](08-global-state-and-sdk.md)

## Status

| Package | Status | Main blocker |
|---|---|---|
| 0 — Validation gates | IN PROGRESS | boundary script can report PASS after check 5 fails |
| 1 — Scheduler determinism | IN PROGRESS | bitwise matrix not implemented |
| 3 — Session isolation | IN PROGRESS | runtime-shared program store and scene hasher |
| 4 — Stable identity | IN PROGRESS | aliases, missing execution identity, wrong graph-instance semantics |
| 5 — Scene program store | BLOCKED | unsafe lease, wrong key, stale call sites |
| 6 — Execution scopes | BLOCKED | child precomp resets parent arena |
| 7 — Software backend | TODO | `SoftwareRenderer` is still the backend |
| 8 — Global state and SDK | TODO | active runtime, process asset root, public software types |

## Immediate P0 repairs

- Fix `session->program_store` call sites to use the actual accessor or final job-owned store API.
- Remove `noexcept` from functions that throw, or make them truly non-throwing.
- Fix `tools/check_architecture_boundaries.sh` so check 5 can fail the script.
- Introduce child execution memory isolation before trusting nested precomp rendering.
- Replace the raw-pointer `ProgramLease` with a lifetime and concurrency guard.

## Permanent target architecture

```text
RenderRuntime
  backend
  scheduler
  catalogs and registries
  engine-lifetime immutable/shared services
  asset resolver

RenderSession
  job history
  dirty history
  job-owned scene program store
  job/pipeline scene hashing state

ExecutionScope
  active arena
  graph and node identity
  root, tile, or precomp lifetime

GraphExecutor
  stateless
  compiled graphs only
  explicit scheduler
  explicit execution scope
```

## Rules

- Complete P0 correctness repairs before backend or SDK cleanup.
- Keep one owner for every mutable cache and history value.
- Never key precomp ownership from composition name alone.
- Never reset a parent arena from nested execution.
- Keep mutable compiled-program refresh and execution under one lease.
- Add targeted tests before removing a TODO.
- Update this status table whenever a package changes state.
