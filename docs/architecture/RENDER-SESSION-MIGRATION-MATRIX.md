# Render-session migration matrix

This document is the ownership ledger for Waves 6–8.  A row is not complete
until the target owner is the only production authority.  Compatibility
accessors are allowed during migration, but they must not introduce another
copy of the state.

| Current field / responsibility | Current owner | Target owner | Wave | Status |
|---|---|---|---:|---|
| `FrameArena` | `RenderSession` | `FrameExecution` / slot payload | 7 | pending payload lease |
| `ExecutionWorkspaceRing` | `RenderSession` | frame execution workspace | 7 | pending executor seam |
| `SceneHasher` | `RenderSession` | compiler fingerprint / runtime cache | 8 | pending scene cleanup |
| `SceneProgramStore` | `RenderSession` | `RuntimeContext` cache | 8 | pending scene cleanup |
| `FrameHistory` | legacy `RenderSession` path | `JobSession` | 6 | new authority introduced; legacy bridge pending |
| `DirtyHistory.previous_layers` | legacy `RenderSession` path | `JobSession` | 6 | new authority introduced; legacy bridge pending |
| dirty-frame counters | `RenderSession` | `FrameExecution` then job aggregate | 7 | compatibility bridge |
| `SessionServices` | `RenderSession` | `RuntimeContext` | 6 | bridge remains |
| `TextLayoutCache` | `RenderSession` | `RuntimeContext` | 6 | pending capability decision |
| `NodeMemoryTracker` | `RenderSession` | job aggregate / frame report | 7 | pending executor seam |
| `RenderErrorSlot` | `RenderSession` | `FrameExecution` | 7 | compatibility bridge |
| `prepared_composition` | `RenderSession` | immutable job program | 6 | removed from `JobSession` |
| authoring name / identity | `RenderSession` | compiler metadata / fingerprint | 8 | pending authoring cleanup |
| authoring compiled composition | `RenderSession` | `CompiledProgram` | 8 | compatibility bridge |
| evaluated frame | prepared-job local payload | frame slot payload | 7 | canonical payload type introduced |
| rendered surface | prepared-job local payload | frame slot payload / handoff | 7 | canonical payload type introduced |

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
to the job-owned history.  The current renderer still has a documented legacy
bridge; the new job path does not use it.

Wave 7 exits when frame payloads are obtained by an explicit slot lease and no
frame-local pointer is retained by `JobSession` after the lease is destroyed.

Wave 8 exits when runtime execution consumes compiled numeric data and no
authoring scene/timeline type is required by the execution kernel.
