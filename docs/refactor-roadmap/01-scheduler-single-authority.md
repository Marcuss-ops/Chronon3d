# Work Package 1 — Remaining scheduler work

## Current state

Completed:

- `GraphExecutor` receives `ExecutionScheduler&` explicitly.
- Executor methods are stateless and `const`.
- Graph execution no longer creates an internal scheduler.
- Tile orchestration routes through `ExecutionScheduler::for_each_index`.
- Graph telemetry reads `scheduler.concurrency()`.

The remaining scheduler test is registered, but it still includes removed `ExecutionPlanCache` APIs and calls the retired raw-graph executor overload. It must be repaired before its results are meaningful.

## TODO

### PR 1.0 — Repair the determinism test API

File:
- `tests/render_graph/executor/test_scheduler_determinism.cpp`

Actions:
- [ ] Remove the deleted `runtime/execution_plan_cache.hpp` include.
- [ ] Remove all `ExecutionPlanCache*` parameters and warm-plan terminology.
- [ ] Compile test graphs through the real `FrameGraphCompiler`.
- [ ] Call only `GraphExecutor::execute(CompiledFrameGraph&, RenderGraphContext&, RenderSession&, ExecutionScheduler&)` until WP-6 changes the scope contract.
- [ ] Keep independent runtime/session/cache state for every scheduler configuration.
- [ ] Make the fake backend write the complete requested framebuffer or region, not only one pixel per 32×32 block.

### PR 1.1 — Complete the bitwise scheduler matrix

Required modes:

- [ ] Sequential
- [ ] TBB fixed 1
- [ ] TBB fixed 2
- [ ] TBB fixed 4
- [ ] TBB automatic

Required cases before WP-6:

- [ ] clear plus shape
- [ ] composite graph
- [ ] effect stack
- [ ] cold node cache
- [ ] warm node cache
- [ ] consecutive frames
- [ ] random-access frames

Required cases after WP-6:

- [ ] tile execution
- [ ] nested precomp
- [ ] precomp inside tile execution

Comparison rules:

- [ ] Compare dimensions exactly.
- [ ] Compare every final channel bit-for-bit.
- [ ] Use stable reference hashes only after a clean baseline run.
- [ ] Do not use epsilon comparisons for final framebuffer output.

### PR 1.2 — Verify Sequential constrains nested parallelism

- [ ] Confirm Sequential mode executes graph levels inside a one-thread arena.
- [ ] Confirm low-level nested TBB kernels cannot escape that arena.
- [ ] Add a maximum-active-worker test.
- [ ] Distinguish orchestration TBB from accepted low-level kernel TBB in documentation.

### PR 1.3 — Add permanent scheduler guards

- [ ] Detect scheduler creation inside graph executor code.
- [ ] Detect direct TBB orchestration in tile execution.
- [ ] Detect production graph execution without an explicit scheduler.
- [ ] Keep documented exceptions limited to scheduler factory and low-level kernels.

## Dependency

Tile/precomp determinism depends on Work Package 6 execution scopes and Work Package 5 program locking.

## Exit criteria

- [ ] The registered deterministic test compiles against the current executor API.
- [ ] All five scheduler modes produce bit-identical output.
- [ ] Sequential mode limits parent, tile, child, and nested kernels.
- [ ] Architecture checks prevent scheduler ownership from drifting.
