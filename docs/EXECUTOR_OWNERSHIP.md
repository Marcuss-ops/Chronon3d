# Render-graph executor ownership

## Decision

`GraphExecutor` is the sole production execution authority. It owns the
frame-level decision of **which execution mode runs** and when a frame succeeds
or fails:

```text
GraphExecutor::execute / execute_with_scope
    ├─ execute_compiled_program       (fully-recorded compiled frame)
    └─ execute_levels                 (reference DAG fallback)
         └─ execute_single_node       (per-node orchestration)
              ├─ evaluate_cache
              ├─ compute_dirty_clip
              ├─ run_node              (node.execute + FB ownership/cache write)
              ├─ emit_node_records
              └─ commit_node_state
```

The independent `CommandPlan` adapter is **not a production executor**. It is
used only by backend registry parity/readiness tests. Its declaration now lives
in `tests/helpers/command_plan_executor.hpp`, outside `include/chronon3d/`.
The implementation remains compiled for ABI compatibility because the committed
libabigail baseline contains the exported `execute_command_plan` symbol. It
dispatches an already constructed test `CommandPlan` through `RenderBackend`; it
does not choose the render-graph execution mode, own frame state, or
participate in the production `GraphExecutor` call graph.

## File-by-file ownership

| File | Owner / responsibility | Decision it owns | Status |
|---|---|---|---|
| `executor.cpp` | `GraphExecutor` | Frame-level mode selection, frame error publication, arena/workspace lifetime, compiled-vs-reference choice, final output | **Single production authority** |
| `executor_levels.cpp` | level scheduler helper | Dispatch each compiled DAG level through the supplied `ExecutionScheduler`; level timing and framebuffer release | Helper; does not select the frame execution mode |
| `node_runner.cpp` | per-node orchestration helper | Early skips, cache evaluation, predicted bounds, node-local context, reusable inputs, telemetry/state commit sequencing | Helper; does not own frame lifecycle or mode selection |
| `node_executor.cpp` | node execution primitive | Call `RenderGraphNode::execute`, propagate node errors, convert `OwnedFB` to `CachedFB`, perform node-cache write | Helper; does not select nodes or schedule levels |
| `command_plan_executor.cpp` | backend registry test adapter | Validate and dispatch a pre-built `CommandPlan` pass-by-pass through `RenderBackend` | **Test-only adapter**, not a competing production executor |

## Call graph evidence

Production callers found at the current source tree:

- `GraphExecutor::execute()` and `execute_with_scope()` call
  `execute_internal()` in `executor.cpp`.
- `execute_internal()` calls either `execute_compiled_program()` or
  `execute_levels()`.
- `execute_levels()` calls `execute_single_node()` in
  `executor_levels.cpp`.
- `execute_single_node()` calls `run_node()` in `node_executor.cpp`.
- `execute_command_plan()` has **no production callers** under `apps/`, `src/`,
  or `tools/`; callers are `tests/registry/test_backend_registry.cpp` through
  `tests/helpers/cpu_gpu_parity.hpp` and `gpu_readiness_gate.hpp`.

## Consolidation rule

Two files are execution debt only when they own the same decision. Under this
rule:

- `node_runner.cpp` and `node_executor.cpp` do not duplicate ownership:
  orchestration and the node execution primitive are separate decisions.
- `executor_levels.cpp` does not duplicate `GraphExecutor`: it schedules a
  level using an injected scheduler and has no frame-mode decision.
- `command_plan_executor.cpp` does not duplicate `GraphExecutor`: it is a
  test-only backend adapter for a different input contract (`CommandPlan`, not
  `CompiledFrameGraph`).

The production dual-executor ambiguity is therefore removed by making the
boundary explicit: `command_plan_executor.cpp` has no production callers and
is retained in the `chronon3d_graph_executor` object target only as an
ABI-compatibility shim. Its declaration is test-only; its legacy symbol is
retained solely to preserve the committed ABI baseline.
