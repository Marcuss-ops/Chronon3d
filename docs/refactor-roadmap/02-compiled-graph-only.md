# Work Package 2 — Compiled graph only

## Goal

Use one production path: `RenderGraph -> FrameGraphCompiler -> CompiledFrameGraph -> GraphExecutor`.

## TODO

### PR 2.0
- [ ] Inventory every executor call using a raw graph.
- [ ] Classify each call as production, debug, benchmark, or test.

### PR 2.1
- [ ] Migrate production callers through the existing frame graph compiler.
- [ ] Preserve current builder, optimizer, and preflight order.

### PR 2.2
- [ ] Add one test-only helper that compiles raw test graphs.
- [ ] Keep the helper outside the installed SDK.

### PR 2.3
- [ ] Retire raw-graph executor overloads.
- [ ] Keep compiled-graph execution as the only production contract.

### PR 2.4
- [ ] Review all `ExecutionPlanCache` uses.
- [ ] Keep it only if a documented owner still needs it.
- [ ] Avoid two independent topology-plan systems.

### PR 2.5
- [ ] Validate output, levels, consumer counts, and empty-graph behavior.
- [ ] Keep topology immutable during execution.

### PR 2.6
- [ ] Test empty, invalid, single-level, multi-level, cache reuse, and multi-frame execution.

### PR 2.7
- [ ] Add boundary checks that enforce compiled-only production execution.

## Exit criteria

- [ ] Executor production API accepts only `CompiledFrameGraph`.
- [ ] `FrameGraphCompiler` is the sole topology-plan producer.
- [ ] Tests compile through the real compiler.
