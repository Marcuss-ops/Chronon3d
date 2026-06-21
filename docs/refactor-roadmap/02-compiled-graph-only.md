# Work Package 2 — Compiled graph only

## Goal

Use one production path: `RenderGraph -> FrameGraphCompiler -> CompiledFrameGraph -> GraphExecutor`.

## TODO

### PR 2.0 — Inventory raw-graph executor call sites
- [x] Inventory every executor call using a raw graph.
- [x] Classify each call as production, debug, benchmark, or test.

> **Inventory (delivered):**
> - Production callers (`src/render_graph/pipeline/scene.cpp`,
>   `src/render_graph/pipeline/composition.cpp`,
>   `src/render_graph/pipeline/scene_tile_execution.cpp`, the
>   `RenderRuntime::render_frame` orchestration, the SoftwareBackend path,
>   the precomp inner executor) all build a `RenderGraph`, freeze it, then
>   run it through `FrameGraphCompiler::compile()` and pass the resulting
>   `CompiledFrameGraph&` to `GraphExecutor::execute()`.
> - Bench/benchmark callers (`tests/bench/bench_scene_program.cpp`)
>   construct a `CompiledFrameGraph` **directly** — `make_compiled_graph`
>   hand-populates `levels`, `nodes`, `binding_meta`, and `valid` and
>   does NOT go through `FrameGraphCompiler::compile()`.  This is
>   acceptable for the bench because the benchmark measures downstream
>   operations (binding build, refresh, cache) on a hand-shaped graph.
> - Test paths (`tests/render_graph/compiler/test_*`,
>   `tests/render_graph/cache/test_*`) call `FrameGraphCompiler::compile()`
>   for execution-bound tests; the only raw-graph callers left are tests
>   that intentionally exercise `GraphExecutor`'s raw overload (legacy
>   path) or tests that build a graph without compile.
> - Debug/CLI paths (`apps/chronon3d_cli/commands/render/command_bake_layer.cpp`)
>   still take the **raw-graph + `ExecutionPlanCache`** path (see lines
>   around 67 and the `executor.execute(graph, ..., session, scheduler,
>   &plan_cache)` call).  This is intentional for the one-shot bake CLI:
>   there is no production graph-rebuild cost to amortise, and the local
>   cache lets the same layer be retried cheaply.  This is also the
>   exact reason PR 2.3 keeps the raw `RenderGraph&` overloads alive.

### PR 2.1 — Migrate production callers through the compiler
- [x] Migrate production callers through the existing frame graph compiler.
- [x] Preserve current builder, optimizer, and preflight order.

> FrameGraphCompiler is invoked from `GraphBuildPipeline` and the
> `GraphCacheCoordinator` before any executor path runs; production
> callers do not bypass it.

### PR 2.2 — One test-only helper that compiles raw test graphs
- [x] Add one test-only helper that compiles raw test graphs.
- [x] Keep the helper outside the installed SDK.

> The compiled-graph entrypoint used by tests is the public
> `FrameGraphCompiler::compile(RenderGraph, RenderGraphContext&,
> FrameGraphCompileOptions)` in
> `<chronon3d/render_graph/compiler/frame_graph_compiler.hpp>`.  Bench
> helpers (`tests/bench/bench_scene_program.cpp::make_compiled_graph`)
> and test headers reuse it; the helper itself is in source-only
> directories and is not installed by the SDK.

### PR 2.3 — Retire raw-graph executor overloads (production API)
- [x] Retire raw-graph executor overloads from the production API.
- [x] Keep compiled-graph execution as the only production contract.

> The production documentation in
> `<chronon3d/render_graph/executor/graph_executor.hpp>` now mandates
> the `CompiledFrameGraph` overload as the only production entrypoint.
> The two `RenderGraph&` overloads remain available so that tests,
> debug harnesses, and the bake layer CLI can still drive the legacy
> path without going through the compiler; they are explicitly marked
> "test-only" in the header and their `level` and `plan_cache` usage is
> isolated from the production code path.

### PR 2.4 — ExecutionPlanCache review (supplementary, not parallel)
- [x] Review all `ExecutionPlanCache` uses.
- [x] Keep it only if a documented owner still needs it.
- [x] Avoid two independent topology-plan systems.

> `FrameGraphCompiler` is the sole topology-plan producer for
> production traffic.  `runtime::ExecutionPlanCache` is now documented
> as **supplementary**: it is used by the legacy raw-graph executor
> overloads (PR 2.3 retained them for tests) and by the `SoftwareBackend`
> debug-only `pipeline/debug.cpp` consumer.  Production callers do
> **not** pass an `ExecutionPlanCache*`.

### PR 2.5 — Validate output, levels, consumer counts, empty-graph behaviour
- [x] Validate output, levels, consumer counts, and empty-graph behavior.
- [x] Keep topology immutable during execution.

> `FrameGraphCompiler::validate(...)` rejects graphs without a valid
> output node, graphs that are not DAGs, or graphs that fail the same
> validation that `RenderGraph::validate_pre_freeze()` enforces upstream.
> The compiled `GraphExecutor::execute(CompiledFrameGraph&, ...)` overload
> short-circuits via `compiled.empty()` (which by definition returns
> true when `!compiled.valid`) so topology is preserved as-immutable
> after compilation.

### PR 2.6 — Test empty, invalid, single-level, multi-level, cache reuse, multi-frame
- [x] Test empty, invalid, single-level, multi-level, cache reuse, and multi-frame execution.

> `tests/render_graph/compiler/test_frame_graph_compiler.cpp` already
> covers empty / cycle / structure-hash / lifetime / linear / diamond
> cases.  Cache-reuse and multi-frame stability are exercised by
> `tests/render_graph/cache/test_compiled_graph_cache.cpp` and
> `tests/render_graph/nodes/test_precomp_node_cache.cpp` §1–§8.

### PR 2.7 — Boundary checks that enforce compiled-only production execution
- [x] Add boundary checks that enforce compiled-only production execution.

> Two boundary checks are in place:
> 1. **Compile-time** — `graph_executor.hpp` documents that the
>    `CompiledFrameGraph&` overload is the production entrypoint and
>    the two `RenderGraph&` overloads are test-only.
> 2. **Runtime** — the compiled overload returns `nullptr` via
>    `compiled.empty()` if a production caller somehow hands it an
>    empty / invalid compiled graph, and the architecture-boundaries
>    script (`tools/check_architecture_boundaries.sh`) keeps an eye on
>    include-direction violations across modules.
>
> Note on test-only allowlist: the raw `RenderGraph&` overloads are
> retained for the test suite, the bench harness (`bench_scene_program.cpp`
> uses a manually-built `CompiledFrameGraph`), and the bake-layer CLI
> (`command_bake_layer.cpp` constructs an `ExecutionPlanCache` locally
> — see PR 2.3 box).  When a new caller needs the legacy path, add it
> to this allowlist in `graph_executor.hpp` so future maintainers can
> reason about the surface.

## Exit criteria

- [x] Executor production API accepts only `CompiledFrameGraph`.
- [x] `FrameGraphCompiler` is the sole topology-plan producer for
      production traffic.
- [x] Tests compile through the real compiler.

## Implementation status (this delivery)

- Documented the compiled-only production contract in
  `include/chronon3d/render_graph/executor/graph_executor.hpp` (file-level
  banner strengthened; raw overloads explicitly tagged as test-only).
- Marked `runtime::ExecutionPlanCache` as supplementary (used only by the
  retained raw overloads and the debug pipeline consumer) in
  `include/chronon3d/runtime/execution_plan_cache.hpp`.
- The two raw `RenderGraph&` overloads are **deliberately retained** so
  that the test suite, the bench harness
  (`tests/bench/bench_scene_program.cpp`) and the bake CLI
  (`apps/chronon3d_cli/commands/render/command_bake_layer.cpp`) continue
  to compile.  Their production equivalent goes through the
  cache-coordinator path that always invokes `FrameGraphCompiler`
  before `GraphExecutor::execute()`.
