# Render-graph executor ownership

## Decision

`GraphExecutor` is the sole execution authority. It owns the
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

`CommandPlan` remains a backend-neutral synchronization/data contract. Its
execution is now a `GraphExecutor::execute()` overload, so backend registry
parity/readiness tests use the same executor authority as graph execution.
There is no independent command-plan adapter or second pass-dispatch symbol.

## File-by-file ownership

| File | Owner / responsibility | Decision it owns | Status |
|---|---|---|---|
| `executor.cpp` | `GraphExecutor` | Frame-level mode selection, frame error publication, arena/workspace lifetime, compiled-vs-reference choice, final output | **Single production authority** |
| `executor_levels.cpp` | level scheduler helper | Dispatch each compiled DAG level through the supplied `ExecutionScheduler`; level timing and framebuffer release | Helper; does not select the frame execution mode |
| `node_runner.cpp` | per-node orchestration helper | Early skips, cache evaluation, predicted bounds, node-local context, reusable inputs, telemetry/state commit sequencing | Helper; does not own frame lifecycle or mode selection |
| `node_executor.cpp` | node execution primitive | Call `RenderGraphNode::execute`, propagate node errors, convert `OwnedFB` to `CachedFB`, perform node-cache write | Helper; does not select nodes or schedule levels |
| `executor.cpp` | `GraphExecutor` | Validate and dispatch a pre-built `CommandPlan` pass-by-pass through `RenderBackend` | **Canonical executor authority** |

## Call graph evidence

Production callers found at the current source tree:

- `GraphExecutor::execute()` and `execute_with_scope()` call
  `execute_internal()` in `executor.cpp`.
- `execute_internal()` calls either `execute_compiled_program()` or
  `execute_levels()`.
- `execute_levels()` calls `execute_single_node()` in
  `executor_levels.cpp`.
- `execute_single_node()` calls `run_node()` in `node_executor.cpp`.
- The plan-level `GraphExecutor::execute()` overload is used by backend
  registry parity/readiness tests and shares the canonical `RenderBackend`
  dispatch surface with the graph execution path.

## Consolidation rule

Two files are execution debt only when they own the same decision. Under this
rule:

- `node_runner.cpp` and `node_executor.cpp` do not duplicate ownership:
  orchestration and the node execution primitive are separate decisions.
- `executor_levels.cpp` does not duplicate `GraphExecutor`: it schedules a
  level using an injected scheduler and has no frame-mode decision.
- `GraphExecutor::execute()` accepts both the compiled frame graph and the
  pre-lowered `CommandPlan`; the latter is a backend contract, not a second
  executor authority.

The production dual-executor ambiguity is removed by deleting the legacy
adapter and keeping both graph and plan-level execution under `GraphExecutor`.
