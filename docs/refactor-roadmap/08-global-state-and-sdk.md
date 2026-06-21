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

### TICKET-014: session_resources compositing
- `include/chronon3d/runtime/render_session.hpp:59` includes
  `<chronon3d/backends/software/software_session_resources.hpp>`.
- The `SoftwareSessionResources` composition is software-only state;
  `RenderSession` should not depend on it.
- Resolution: Move the composition to live only on `SoftwareRenderer`
  (and via `SoftwareRenderSession` wrapper if needed).  Track in WP-8.

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

## Exit criteria

- `tests/architecture/test_render_session_includes_boundary.py` reports
  `errors=0` with NO `KNOWN_VIOLATIONS` entries remaining.
- Turbo build (`./build-fast.sh turbo`) succeeds.
- `git grep RenderFrameInfo` returns zero hits in `include/`, `src/`,
  `tests/`, `apps/`.
- `git grep 'plan_cache'` returns zero hits in `include/`,
  `src/render_graph/executor/`, `apps/chronon3d_cli/commands/render/`,
  and the boundary-test files in `tests/render_graph/`.
