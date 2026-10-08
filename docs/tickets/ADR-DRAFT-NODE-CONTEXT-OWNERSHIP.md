# ADR DRAFT — Owner-scoped payload references for node execution contexts

Status: **DRAFT** (documentation only; no code change in this pass).
Related: `TICKET-HOTPATH-ZERO-ALLOC-P1` deferred item D3.

## Context

`clone_for_node_execution` (render-graph context clone) copies three
`std::shared_ptr` members per node per frame: `static_context`, `frame_error`
(now a raw pointer, see D2 below), and `processor_snapshot`. Each copy is an
atomic `LOCK XADD` on the control block, so a scene with ~150 nodes over
~18 000 frames pays for millions of atomic operations that protect payloads
whose lifetime the job already guarantees.

D2 status (verified 2026-10-08): `RenderGraphContext::frame_error` is already a
non-owning raw pointer to a payload-owned or stack-owned `RenderErrorSlot`
(`executor_internal.cpp`); no `make_shared` remains on that path. The ticket
text describing D2 as open is stale.

## Decision (proposed)

Introduce an owner-scoped execution context:

- `JobExecutionContext` owns immutable per-job state (`static_context`,
  `processor_snapshot`) for the lifetime of one render job.
- `NodeExecutionContext` holds `const StaticContext*` and
  `const ProcessorSnapshot*`, valid for the job lifetime, and copies only the
  POD `FrameInput` by value (as today).

## Consequences

- Removes three `LOCK XADD` per node×frame from the hot path.
- Lifetime becomes a job invariant that must be enforced by construction: a
  node context must never outlive its `JobExecutionContext`. The ADR must name
  the owner and the check (debug assertion on use after job teardown).
- Affects every node that reads `ctx.static_context` / `ctx.processor_snapshot`;
  the change must be mechanical, with an equivalence test over the render
  graph corpus.

## Exit conditions before implementation

1. Measured per-node clone cost on the overlay matrix (CPU profile, not
   inferred).
2. Owner-lifetime assertion designed and reviewed.
3. Equivalence test: identical output bytes on the golden corpus.

Until these hold, D3 stays deferred.
