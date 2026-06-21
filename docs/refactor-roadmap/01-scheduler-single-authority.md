# Work Package 1 — One scheduler authority

## Goal

Make `RenderRuntime::scheduler()` the only scheduler used by graph execution, tile execution, and nested precompositions.

## Current problem

`GraphExecutor::execute()` still creates a new default scheduler internally. The runtime-owned scheduler therefore does not control actual execution.

## TODO

### PR 1.0 — Change the executor contract

Files:
- `include/chronon3d/render_graph/executor/graph_executor.hpp`
- `src/render_graph/executor/executor.cpp`

Actions:
- [ ] Add `ExecutionScheduler& scheduler` to every active executor overload.
- [ ] Make executor methods `const` after confirming no member state is mutated.
- [ ] Remove internal `make_execution_scheduler(...)` calls.
- [ ] Pass the received scheduler to `execute_levels(...)`.
- [ ] Keep `FrameArena* arena_override` temporarily; Work Package 6 removes it.

Target signature:

```cpp
std::shared_ptr<Framebuffer> execute(
    CompiledFrameGraph& compiled,
    RenderGraphContext& ctx,
    RenderSession& session,
    ExecutionScheduler& scheduler,
    FrameArena* arena_override = nullptr
) const;
```

### PR 1.1 — Update production call sites

Search all executor invocations and update them.

Expected sources include:
- graph pipeline coordinator
- tile execution coordinator
- precomposition execution
- renderer/debug paths
- tests and fixtures

Actions:
- [ ] Production code passes `RenderRuntime::scheduler()`.
- [ ] `SoftwareRenderer` forwards its runtime scheduler by reference.
- [ ] Precomp temporarily uses `*ctx.services.scheduler`.
- [ ] A missing scheduler is treated as a wiring error in production.
- [ ] Isolated tests create an explicit scheduler.

### PR 1.2 — Move tile parallelism behind the scheduler

File:
- `src/render_graph/pipeline/scene_tile_execution.cpp`

Actions:
- [ ] Replace direct `tbb::parallel_for` with `ExecutionScheduler::for_each_index` or the existing scheduler primitive.
- [ ] Preserve the current sequential branch.
- [ ] Preserve coalesced-region accounting and telemetry.
- [ ] Do not create a fallback scheduler inside the tile path.
- [ ] When no scheduler exists in a test-only context, run sequentially or fail clearly.

### PR 1.3 — Fix telemetry ownership

Actions:
- [ ] Report scheduler concurrency from the scheduler actually used.
- [ ] Remove `hardware_concurrency()` from graph execution telemetry.
- [ ] Ensure nested graph telemetry reports the same scheduler mode as its parent.

**Implementation status (this PR):**

| File | Role | Uses scheduler.concurrency()? | Notes |
| --- | --- | --- | --- |
| `src/render_graph/executor/executor_levels.cpp` (max_workers) | graph execution telemetry | YES | `static_cast<int64_t>(scheduler.concurrency())` — already correct. |
| `src/runtime/telemetry/telemetry_manager.cpp:243` | engine-level telemetry (not graph execution) | NO | reads `std::thread::hardware_concurrency()` for a startup banner value; out of WP1 scope. |
| `src/core/framebuffer_clear_parallel.cpp:48` | low-level parallel clear kernel | NO | accepted; documented in PR 1.5 as low-level kernel TBB use. |
| `src/core/scheduler/execution_scheduler.cpp:62` | scheduler factory fallback for TbbFixed with `worker_count == 0` | NO | legitimate — the scheduler is the SOLE owner of this fallback. |
| `apps/chronon3d_cli/main.cpp:21` | CLI thread-pool sizing for out-of-render workers | NO | accepted; CLI-internal, not graph execution. |

The graph execution telemetry path (`executor_levels.cpp`) already reports
the scheduler-concurrency-correct value because the WP1 PR 1.0+1.1
change threaded `ExecutionScheduler&` into `execute_levels(...)`.  The
other four sites are intentionally NOT under WP1 PR 1.3 because they
are not graph execution telemetry.  PR 1.3 is COMPLETE for the graph
execution path; the remaining sites are documented exceptions.

### PR 1.4 — Add scheduler-swap determinism tests

Create a dedicated test file under `tests/render_graph/executor/` or `tests/deterministic/`.

Required comparisons:
- [ ] Sequential vs TBB fixed 1.
- [ ] Sequential vs TBB fixed 2.
- [ ] Sequential vs TBB fixed 4.
- [ ] Sequential vs TBB automatic.

Required scenes:
- [ ] clear plus shape
- [ ] composite
- [ ] one effect stack
- [ ] warm cache and cold cache
- [ ] tile execution

Comparison rule:
- [ ] Compare framebuffer dimensions.
- [ ] Compare every channel bit-for-bit.
- [ ] Do not use floating-point tolerance for the final framebuffer.

### PR 1.5 — Add architecture checks

- [ ] Boundary script fails if `make_execution_scheduler` appears under `src/render_graph/executor`.
- [ ] Boundary script fails if direct `tbb::parallel_for` returns in the tile coordinator.
- [ ] Document any TBB use that remains inside low-level kernels.

## Tests

- graph executor tests
- tile parallel tests
- deterministic tests
- fast suite
- no-content build

## Exit criteria

- [ ] Runtime scheduler is passed explicitly to every graph execution.
- [ ] Executor creates no scheduler.
- [ ] Tile orchestration does not call TBB directly.
- [ ] Sequential mode controls the whole graph and tile orchestration.
- [ ] Scheduler-swap tests are bit-identical.

## Out of scope

- Removing raw `RenderGraph` overloads.
- Removing `FrameArena* arena_override`.
- Moving precomp cache ownership.
- Reworking backend raster kernels.
