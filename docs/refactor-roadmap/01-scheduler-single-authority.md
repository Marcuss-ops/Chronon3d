# Work Package 1 — Remaining scheduler work

## Current state

The runtime scheduler is already passed explicitly to `GraphExecutor`, the executor no longer constructs a scheduler, and tile orchestration uses `ExecutionScheduler::for_each_index`.

## TODO

### PR 1.0 — Verify scheduler telemetry

- [ ] Report concurrency from the scheduler actually used.
- [ ] Remove any execution telemetry based directly on `std::thread::hardware_concurrency()`.
- [ ] Ensure nested precomp telemetry reports the parent scheduler mode and concurrency.
- [ ] Ensure local test-only sequential schedulers are clearly identified in telemetry.

### PR 1.1 — Add the bitwise scheduler matrix

Compare independent render jobs using:

- [ ] Sequential
- [ ] TBB fixed 1
- [ ] TBB fixed 2
- [ ] TBB fixed 4
- [ ] TBB automatic

Required scenes:

- [ ] clear plus shape
- [ ] composite graph
- [ ] effect stack
- [ ] cold and warm node cache
- [ ] tile execution
- [ ] nested precomp
- [ ] precomp inside tile execution
- [ ] consecutive frames and random-access frames

Rules:

- [ ] Use independent runtime, session, and cache state per scheduler.
- [ ] Compare framebuffer dimensions exactly.
- [ ] Compare every final channel bit-for-bit.
- [ ] Do not use epsilon comparisons for final pixels.

### PR 1.2 — Constrain nested TBB in Sequential mode

- [ ] Verify `SchedulerMode::Sequential` constrains nested TBB kernels.
- [ ] Use a one-thread task arena if plain sequential loops do not constrain nested library parallelism.
- [ ] Add a test that records maximum active worker count.

### PR 1.3 — Add permanent guards

- [ ] Guard against scheduler construction inside graph executor.
- [ ] Guard against direct TBB orchestration in tile execution.
- [ ] Document allowed low-level TBB kernels separately from orchestration.

## Dependency

The nested-precomp and precomp-inside-tile cases depend on Work Package 6 execution scopes.

## Exit criteria

- [ ] Scheduler telemetry is sourced from the active scheduler.
- [ ] Sequential mode controls parent, tile, child, and nested kernels.
- [ ] The complete scheduler matrix is bit-identical.
- [ ] Architecture checks prevent scheduler ownership from drifting.
