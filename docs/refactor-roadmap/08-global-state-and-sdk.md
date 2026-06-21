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
