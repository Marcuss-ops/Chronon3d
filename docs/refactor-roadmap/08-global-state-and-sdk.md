# Work Package 8 — Global State Removal and SDK Polish

## Goal

Eliminate remaining engine-generic headers' cross-layer dependencies and
process-wide globals; refine the SDK boundary so that engine-generic and
backends do not leak transitively.

## Scope (continuation from WP-7)

### TICKET-009 follow-up: PR-2 rewire enforcement
- Drop `execute(RenderGraph&, …)` overloads on `GraphExecutor` (cleanup
  target — overloads exist on `c51e6472`'s snapshot of `origin/main`
  pre-cross-cutting-cleanup; to be removed once the call-site migration
  is complete).
- Drop `runtime::ExecutionPlanCache* plan_cache` parameter from
  `GraphExecutor::execute` on the `CompiledFrameGraph` path (the
  parameter is currently unused on that path and is `(void)`'d out).
- Migrate the 3 callers (`command_bake_layer`, `debug`,
  `test_render_backend`) + the 3 advanced callers (`scene_tile_execution`,
  `tile_execution_coordinator`, `precomp_node_execute`) to drop the
  legacy arg.
- Preserve `ExecutionPlanCache` infrastructure owned by `RenderRuntime` /
  `scene_program_cache` / `precomp_node` (active for those subsystems).

### TICKET-013: scene_hasher leak
- `include/chronon3d/runtime/render_session.hpp:42` includes
  `<chronon3d/render_graph/core/scene_hasher.hpp>`.  Symbol `SceneHasher`
  is used as a field.
- Resolution: Move `scene_hasher` ownership off `RenderSession` to
  `RenderRuntime` (matching the TICKET-011 ownership boundary) so
  `render_session.hpp` becomes engine-generic again.  This is part of the
  WP-8 deliverable tracked by this doc.
- ✅ RESOLVED — scene_hasher relocated to RenderRuntime (value member
  `m_owned_scene_hasher`).  `RenderSession` reaches it via a
  `services.scene_hasher` back-pointer populated by
  `runtime::make_session()`.  `RenderRuntime::services()` +
  `RenderRuntime::scene_hasher()` are the canonical access paths.
  `render_session.hpp` now forward-declares `graph::SceneHasher`
  instead of including the full header.  See
  `src/runtime/render_runtime.cpp` for `populate()` wiring and
  `src/runtime/render_session.cpp` for the session-side proxy accessors
  + `reset_job()` body.

### TICKET-014: session_resources compositing
- `include/chronon3d/runtime/render_session.hpp:59` includes
  `<chronon3d/backends/software/software_session_resources.hpp>`.
- The `SoftwareSessionResources` composition is software-only state;
  `RenderSession` should not depend on it.
- Resolution: Move the composition to live only on `SoftwareRenderer`
  (and via `SoftwareRenderSession` wrapper if needed).  Track in WP-8.
- ✅ RESOLVED — the legacy use of `SoftwareSessionResources` inside
  `render_session.hpp` was eliminated when WP-3 PR 3.4 closed-out
  removed the legacy `SoftwareRenderSession` struct from this header.
  Post-WP-3 the only remaining reference was inside a doc-comment
  block, so the include was dropped entirely.  The canonical struct
  lives at
  `<chronon3d/backends/software/software_session_resources.hpp>` and
  is reachable via the canonical
  `<chronon3d/backends/software/software_render_session.hpp>` that
  `software_renderer.hpp` includes directly.  Header is now free of
  any `backends/software/` include.

### TICKET-015: legacy aliases cleanup
- Drop `using RenderFrameInfo = FrameInput;` alias (back-compat shim
  for legacy callers).
- Drop `default_assets_root_for_deep_code()` free function; inline the
  lookup directly in `runtime::resolve_asset_path`.

### TICKET-016: architecture boundary enforcement
- Add `tests/architecture/test_render_session_includes_boundary.py` —
  Python-based include-graph validator.  Landed in this work-package (see
  `dfccb258` / `e5fd514a` in the local commit log).
- Wire it as an optional CTest target so CMake runs it on demand.
- Extend as more checks are added (e.g. asset-locale layer).



### TICKET-017: scene_program_store leak
- `include/chronon3d/runtime/render_session.hpp:43` includes
  `<chronon3d/render_graph/cache/scene_program_store.hpp>`.  The
  `graph::SceneProgramStore` is referenced as a field on `RenderSession`.
- Resolution: move `program_store` ownership off `RenderSession` to
  `RenderRuntime` (matching the TICKET-011 ownership boundary and the
  TICKET-013/014 decoupling pattern).  Tracked in WP-8.
- ✅ RESOLVED — `program_store` ownership moved to RenderRuntime as
  `m_owned_program_store` (unique_ptr).  Session reaches it via
  `services.program_store` back-pointer populated by
  `runtime::make_session()`.  `render_session.hpp` forward-declares
  `graph::SceneProgramStore` only; full type lives in
  `src/runtime/render_runtime.cpp` (populate) and
  `src/runtime/render_session.cpp` (session-side reset_job forward
  to `services.program_store->clear()`).

## Exit criteria


- `tests/architecture/test_render_session_includes_boundary.py` reports
  `errors=0` with NO `KNOWN_VIOLATIONS` entries remaining.
- Turbo build (`./build-fast.sh turbo`) succeeds.
- `git grep RenderFrameInfo` returns zero hits in `include/`, `src/`,
  `tests/`, `apps/`.
- `git grep 'plan_cache'` returns zero hits in `include/`,
  `src/render_graph/executor/`, `apps/chronon3d_cli/commands/render/`,
  and the boundary-test files in `tests/render_graph/`.

## WP-8 close-out snapshot

The three structural dependencies on `render_session.hpp` are now
resolved:

| TICKET | State | Notes |
|--------|-------|-------|
| TICKET-013 (`scene_hasher`)      | RESOLVED | Field on `RenderSession` → `RenderRuntime::m_owned_scene_hasher`; session access via `services.scene_hasher` pointer. |
| TICKET-014 (`software_session_resources`) | RESOLVED | Include dropped — only doc-comment referenced it post WP-3 PR 3.4. |
| TICKET-017 (`scene_program_store`) | RESOLVED | Field on `RenderSession` → `RenderRuntime::m_owned_program_store` (unique_ptr); session access via `services.program_store` pointer. |

The boundary test
(`tests/architecture/test_render_session_includes_boundary.py`) now
has an empty `KNOWN_VIOLATIONS` dict; the test outputs
`OK: include-graph boundary invariants satisfied (errors=0).` with
zero `INFO:` lines.

**Shared-state consequence** — TICKET-013 + TICKET-017 each
relocate state that was previously per-RenderSession to
RenderRuntime.  In single-engine-instance deployments (one
runtime + one renderer + one session) this is semantically
indistinguishable from the previous per-session isolation.
Deployments that share a single RenderRuntime across multiple
SoftwareRenderers / SoftwareRenderSessions will see
scene_hasher + program_store SHARED across those instances (see
the CHANGELOG R5 "Shared-state note" for the workaround).

## Audit §9.4 closure note (PR-2 rewire close-out)

The original text that anchored the §9.4 audit item lived in a
doc-comment inside `src/render_graph/pipeline/scene.cpp` (around
Phase 4 of `render_scene_via_graph`).  Aggressive
`plan_cache`-line-stripping during the PR-2 rewire close-out
(commit `9f9af90e`) removed that comment by accident, which would
have orphaned the §9.4 reference entirely.  Capturing it here as
documentation so the audit anchor survives.

### Original text (verbatim, pre-close-out)
```
    // PR-A removed the ExecutionPlan cache that used to gate on this flag
    // inside GraphExecutor; the flag survives for the downstream
    // coordinator and will be re-paired with a stable fast-path in a
    // future PR (audit §9.4).
```

### What the close-out did

- The `chronon3d::runtime::ExecutionPlanCache` class is RETIRED
  (commit `9f9af90e` on `origin/main`).
- `RenderPolicy::graph_structure_unchanged` (the flag the audit
  refers to) is still SET by `render_scene_via_graph` (Phase 4 of
  `src/render_graph/pipeline/scene.cpp`), but no production
  consumer currently reads it post-PR-2 rewire.  Its sole pre-rewire
  consumer was the executor's plan-cache fast-path branch in
  `execute(RenderGraph&, ...)` — that overload is gone; the
  surviving `execute(CompiledFrameGraph&, ...)` overload consults
  `compiled.levels` directly and does not inspect the flag.
- `FrameGraphCompiler::compile` does NOT yet honour
  `ctx.policy.graph_structure_unchanged` either — the compiler
  body consults only `ctx.policy.skip_initial_clear` and
  `ctx.node_exec.early_exit_skip` from the policy/exec.  The
  audit §9.4 intent ("stable fast-path") therefore remains a
  FUTURE FrameGraphCompiler enhancement, not a current behaviour.
- Practical consequence: the flag is DORMANT for now.  Writers
  should keep setting it (binary backward-compatibility) but no
  reader reacts.  A future PR will need to teach
  `FrameGraphCompiler::compile` to consult the flag and skip
  `build_execution_levels` + `build_node_metadata` when the
  caller asserts `graph_structure_unchanged && same(hash)`.

### Status of §9.4

- §9.4 is **dormant**, not closed.  The audit predicate
  (`graph_structure_unchanged`) is preserved in the policy for
  backward-compatibility but UNREAD by today's production paths.
  The executor's plan-cache fast-path branch (which used to
  consume it) retired in PR-2 rewire; the compiler does not yet
  honour it either.
- Closure path: a future PR that adds a structural-reuse
  fast-path to `FrameGraphCompiler::compile` will re-attach
  §9.4 to a live reader without re-introducing
  `runtime::ExecutionPlanCache`.  Until then, the audit item
  should be tracked as "dormant — awaiting stability-aware
  fast-path work in FrameGraphCompiler" rather than closed.
- The historical reference chain
  (`runtime::ExecutionPlanCache` |
  `runtime::RendererRuntimeResources::plan_cache`) is fully
  closed (both classes were retired in PR-2 rewire).  Only §9.4
  — and §9.4 specifically — remains dormant.

### Where the work lives now

- `include/chronon3d/render_graph/render_graph_context.hpp` —
  defines the dormant `RenderPolicy::graph_structure_unchanged`
  field (kept for backward compatibility; future compiler work
  will start by adding the reader here).
- `src/render_graph/pipeline/scene.cpp` — the only writer in
  the codebase today (Phase 4 of `render_scene_via_graph`).
- `include/chronon3d/render_graph/compiler/frame_graph_compiler.hpp`
  / `src/render_graph/compiler/frame_graph_compiler.cpp` — the
  intended CONSUMER for the §9.4 stable-fast-path intent.  Today
  the compiler does not consult the flag; the predicate is the
  literal placeholder for the future work item.
- `include/chronon3d/render_graph/executor/graph_executor.hpp`
  / `src/render_graph/executor/executor.cpp` — the old plan-cache
  reader (now retired); the surviving
  `execute(CompiledFrameGraph&, ...)` overload does NOT consult
  the flag.
- `docs/CHANGELOG.md` R6 entry — External SDK migration note +
  the body bullets for RenderGraph& overloads +
  ExecutionPlanCache retirement.
