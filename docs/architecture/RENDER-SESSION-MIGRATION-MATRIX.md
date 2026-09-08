# Render-session migration matrix

This document is the ownership ledger for Waves 6–8.  A row is not complete
until the target owner is the only production authority.  Compatibility
accessors are allowed during migration, but they must not introduce another
copy of the state.

Reconciled against the 2026-09-07 architecture audit (JobSession owner closed,
FrameExecution ~75–80%, Text cache SSOT closed, Scene/Timeline evacuation and
Render-engine demolition in progress).  Rows below no longer name
`RenderSession`, which has been deleted — the only remaining trace is a
forbidden-pattern rule.

| Current field / responsibility | Current owner | Target owner | Wave | Status |
|---|---|---|---:|---|
| `FrameArena` | `FrameExecution` payload for leased production frames; `JobSession` fallback for standalone callers | `FrameExecution` / slot payload | 7 | production arena handoff landed 2026-09-08; standalone fallback remains |
| `ExecutionWorkspaceRing` | `JobSession` | frame execution workspace | 7 | JobSession-owned; pending executor seam (Wave 4) |
| `SceneHasher` | `JobSession` | compiler fingerprint / runtime cache | 8 | JobSession-owned; evacuation pending (Wave 7) |
| `SceneProgramStore` | `JobSession` | `RuntimeContext` cache | 8 | JobSession-owned; evacuation pending (Wave 7) |
| `FrameHistory` | `JobSession` | `JobSession` | 6 | DONE — sole history authority; legacy bridge deleted with `RenderSession` |
| `DirtyHistory.previous_layers` | `JobSession` | `JobSession` | 6 | DONE — JobSession owns dirty telemetry |
| dirty-frame counters | `FrameExecution` per-frame / `JobSession` aggregates | `FrameExecution` then job aggregate | 7 | `RenderSession` deleted; promotion path live |
| services locator (`SessionServices` → `RenderServices`) | deleted with `RenderSession`; `RenderServices` locator remains | `RuntimeContext` services | 5 | locator demolition open (Wave 5) |
| `TextLayoutCache` | `RuntimeContext` | `RuntimeContext` | 6 | DONE — `render_runtime.hpp` owns `m_text_layout_cache`; no process-global, no JobSession cache |
| `NodeMemoryTracker` | `JobSession` | job aggregate / frame report | 7 | JobSession-owned; pending executor seam (Wave 4) |
| `RenderErrorSlot` | `JobSession` | `FrameExecution` | 7 | JobSession-owned; pending executor seam (Wave 4) |
| `prepared_composition` | removed | immutable job program (`CompiledProgram`) | 6 | DONE — `JobSession` holds only `CompiledProgram` |
| authoring name / identity | `CompiledProgram` metadata | compiler metadata / fingerprint | 8 | `JobSession` holds `CompiledProgram`; evacuation pending |
| authoring compiled composition | `CompiledProgram` retained legacy view (Wave-2 plan bridge) | `CompiledProgram` numeric IR | 8 | compatibility bridge only — see `include/chronon3d/compiler/detail/compiled_program_plan_bridge.hpp` |
| evaluated frame | frame slot payload | frame slot payload | 7 | canonical slot lease landed; payloads are non-nullable slot refs |
| rendered surface | frame slot payload / handoff | frame slot payload / handoff | 7 | canonical payload type live |

## Reconciliation log (2026-09-07)

One line per ledger change; extended record in the Wave 7-9 compiler-boundary
ticket and the audit wave table (ephemeral, not stored in-tree).

1. `FrameArena` current owner: `RenderSession` → `JobSession` (arena_ptr); target/status unchanged (Wave 4 audit).
2. `ExecutionWorkspaceRing` current owner: `RenderSession` → `JobSession` (execution_workspaces).
3. `SceneHasher` current owner: `RenderSession` → `JobSession` (scene_hasher_state) — commit deliberately unified it there; evacuation is Wave 7 debt.
4. `SceneProgramStore` current owner: `RenderSession` → `JobSession` (program_store_state).
5. `FrameHistory` current owner: `legacy RenderSession path` → `JobSession`; status DONE (JobSession owner closed; `RenderSession` deleted).
6. `DirtyHistory.previous_layers` current owner → `JobSession`; status DONE.
7. dirty-frame counters current owner → `FrameExecution`/`JobSession` (RenderSession deleted).
8. `SessionServices` row: entity deleted with `RenderSession`; row reframed as services-locator debt (Wave 5).
9. `TextLayoutCache` current owner → `RuntimeContext`; status DONE (capability decision landed — `render_runtime.hpp:325`, no process-global, no JobSession cache).
10. `NodeMemoryTracker` / `RenderErrorSlot` current owner → `JobSession`; Wave 7 pending seams unchanged.
11. `prepared_composition` row: DONE — JobSession holds `CompiledProgram` only (no authoring composition).
12. authoring name/identity + authoring compiled composition rows: current owner → `CompiledProgram`; compatibility view now routed through the Wave-2 internal plan bridge.
13. evaluated frame / rendered surface rows: status updated to slot-lease model (non-nullable slot refs, `PreparedRenderJob` no longer owns a second lease/queue protocol).

## Invariants

1. `JobSession` owns only state that may be observed by multiple frames of the
   same job: immutable program, range, cancellation, temporal history and
   pipeline authorities.
2. `FrameExecution` owns only the active frame lease and frame-local counters,
   errors and temporary views.  It does not own job history.
3. `RuntimeContext` is process/engine lifetime and is borrowed by jobs.
4. A compatibility bridge may forward to the canonical owner, but may not keep
   a second mutable copy.
5. `PreparedRenderJob` orchestrates `FrameSlotPool`, `FrameQueue` and
   `GpuCompletionTracker`; it does not create another lifecycle protocol.

## Exit criteria

Wave 6 exits when no `JobSession` field contains the generic
`RenderSession` facade and the production renderer binds its history accessors
to the job-owned history.  **Met 2026-09-07:** `RenderSession` is deleted
(forbidden-pattern rule only) and `JobSession` is the sole history authority;
the legacy bridge is gone with the session type.

Wave 7 exits when frame payloads are obtained by an explicit slot lease and no
frame-local pointer is retained by `JobSession` after the lease is destroyed.
The slot/payload lease is live and production graph execution now binds its
root scope to `FrameExecution::arena()`; `JobSession` still owns the explicit
standalone fallback plus `ExecutionWorkspaceRing`, `NodeMemoryTracker` and
`RenderErrorSlot`, which remain open migration seams.

Wave 8 exits when runtime execution consumes compiled numeric data and no
authoring scene/timeline type is required by the execution kernel.  The
`CompiledProgram` retained legacy view is confined behind the Wave-2 internal
plan bridge until this exits.
