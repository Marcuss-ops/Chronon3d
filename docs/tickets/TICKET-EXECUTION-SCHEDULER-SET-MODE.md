# TICKET-EXECUTION-SCHEDULER-SET-MODE \u2014 Scheduler mode API for matrix dim

## Stato: OPEN (2026-07-20, opened atomicamente with Batch 1.5 PARTIAL commit)

## Problema

TICKET-GOLDEN-MATRIX-SUBTITLE-BATCH-1 Batch 1.5 PARTIAL closed bg dimension
via `CompositionSpec::background_color_rgba` (canonical user-spec Option (a)),
but the scheduler dimension was shelved because:

- `chronon3d::ExecutionScheduler` has NO `set_mode(SchedulerMode)` API.
- The scheduler mode is frozen at ctor: `ExecutionScheduler(SchedulerMode mode, int worker_count, bool pin_calling_thread)`.
- `SoftwareRenderer::scheduler()` returns a non-const reference, but the
  underlying `ExecutionScheduler` does not expose a mode-setter \u2014 only
  scheduler query helpers (`mode()`, `name()`).

Wiring a per-cell scheduler mode in the matrix requires either (a) a
new scheduler API, or (b) a renderer ctor parameter \u2014 both are ADR-grade.

## Soluzione accettabile (deferred \u2014 requires ADR)

### Option (a): Add `ExecutionScheduler::set_mode(SchedulerMode)`

- Pros: smallest API-blur; existing accessor pattern `SoftwareRenderer::scheduler()` already returns a mutable reference.
- Cons: requires thread-safety audit (TBB `task_arena` resizing on live scheduler is non-trivial); requ
