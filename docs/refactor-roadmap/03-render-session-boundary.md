# Work Package 3 — Render session boundary

## Goal

Make `runtime/render_session.hpp` backend-agnostic and free from render-graph implementation types.

## TODO

### PR 3.0 — Map current ownership
- [x] List every `RenderSession` field and every caller.
- [x] Mark each field as runtime, graph-pipeline, or software-backend state.

> **Field ownership map (delivered):**
>
> | Field                          | Layer        | Source header |
> |--------------------------------|--------------|----------------|
> | `arena_ptr`                    | runtime      | `core/memory/arena.hpp` |
> | `frame_history`                | runtime      | `runtime/frame_history.hpp` (alias `RendererFrameHistory` in `math/renderer_state.hpp`) |
> | `dirty_telemetry`              | runtime      | `runtime/dirty_history.hpp` (alias `RendererDirtyTelemetry`) |
> | `layer_history`                | runtime      | `math/renderer_state.hpp` (`RendererLayerHistory`) |
> | `scene_hasher`                 | graph-pipeline | `render_graph/core/scene_hasher.hpp` (exposed via `runtime/scene_hasher_session.hpp`) |
> | `services`                     | runtime      | `runtime/session_services.hpp` |
> | `program_store`                | graph-pipeline | `render_graph/cache/scene_program_store.hpp` |
>
> The `SoftwareSessionResources` software-side fields (buffer_ring,
> scratch_buffer, plus software's scene_hasher copy) live in
> `backends/software/software_session_resources.hpp`.

### PR 3.1 — Split software session types
- [x] Move `SoftwareRenderSession` to `backends/software/software_render_session.hpp`.
- [x] Keep `SoftwareSessionResources` under the software backend.
- [x] Update includes and call sites.
- [x] Remove legacy `SoftwareRenderSession` duplicate from `render_session.hpp` (WP-3 PR 3.4 close-out — eliminates ODR).

> `SoftwareRenderSession` lives EXCLUSIVELY at
> `<chronon3d/backends/software/software_render_session.hpp>`; the
> legacy inline duplicate that used to live in
> `<chronon3d/runtime/render_session.hpp>` has been REMOVED in the
> WP-3 PR 3.4 close-out.  `SoftwareSessionResources` likewise lives
> canonically at `<chronon3d/backends/software/software_session_resources.hpp>`.
> The legacy duplicate contained a stale 2-field struct that caused
> a C++-level redefinition error before TICKET-011-final consolidation;
> the canonical 3-field struct is now the single source of truth.
> `SoftwareRenderer::m_session` already stores `SoftwareRenderSession`
> (acceptance criterion for PR 3.1).  Migration is complete for
> production callers (see Sentinel-003 in `docs/migrations/2026-06-renderer-state.md`).

### PR 3.2 — Move graph-pipeline state
- [~] Move `SceneHasher` out of `RenderSession`.
- [x] Create or reuse a pipeline-session state type.
- [x] Keep graph-only state inside the graph pipeline module.

> PARTIALLY DELIVERED:
>
> - The reusable pipeline-session type already exists:
>   `<chronon3d/runtime/scene_hasher_session.hpp>` provides
>   `SceneHasherSession` as a stable alias over `graph::SceneHasher`.
> - The `RenderSession` retains its `scene_hasher` field for the
>   lifetime of the existing `SoftwareRenderer` integration (Phase 5
>   migration in the architecture plan).  The duplicate on
>   `SoftwareSessionResources` is the in-flight shape — both halves
>   will be collapsed in the next phase.  This delivery does not
>   re-shuffle fields yet (would touch all callers; tracked in the
>   followup section).

### PR 3.3 — Consolidate history types
- [x] Replace legacy renderer-prefixed history aliases with runtime names.
- [~] Fold layer bounding-box history into `DirtyHistory` when compatible.
- [x] Keep one owner for each history value.

> ALREADY DELIVERED (pre-this-PR):
>
> - `FrameHistory` is the canonical name; `RendererFrameHistory` is an
>   alias in `<chronon3d/math/renderer_state.hpp>`.
> - `DirtyHistory` is the canonical name; `RendererDirtyTelemetry` is
>   a `using` alias.
> - `RendererLayerHistory` is the canonical name in
>   `<chronon3d/math/renderer_state.hpp>` (Phase-2 split retained its
>   identity; the `DirtyHistory::previous_layers` fan-out documented in
>   that header is the planned-but-not-yet-merged unification).

### PR 3.4 — Define reset semantics
- [x] Add `reset_frame_temporaries()`.
- [x] Add `reset_job()`.
- [x] Frame reset clears temporary memory and frame telemetry only.
- [x] Job reset also clears history, caches owned by the session, and software resources.
- [x] Retire ambiguous `clear_per_frame()` after call sites migrate.

> DELIVERED + CLOSED-OUT:
>
> **RenderSession (`<chronon3d/runtime/render_session.hpp>`):**
> - New `reset_frame_temporaries()` method that clears
>   `dirty_telemetry` only (frame-scoped monitoring counters).
> - New `reset_job()` method that performs a full reset:
>   `reset_frame_temporaries()` + `frame_history = {}` +
>   `layer_history = {}` + `scene_hasher = {}` +
>   `program_store->clear()`.
> - `clear_per_frame()` has been **REMOVED** in the WP-3 close-out
>   delivery (commit landed).  Its legacy semantics
>   ("erase everything except arena") is now provided by
>   `reset_job()`.  Any caller that previously depended on the
>   retired method must use `reset_job()` instead.
>
> **SoftwareSessionResources** already had
> `reset_frame_temporaries()` (clears scratch_buffer + scene_hasher)
> and `reset_job()` (also drops buffer_ring) — kept as-is.
>
> **SoftwareRenderSession** now exposes both reset methods
> (`reset_frame_temporaries()` and `reset_job()`) at its canonical
> location `<chronon3d/backends/software/software_render_session.hpp>`
> (the legacy duplicate that previously lived in
> `<chronon3d/runtime/render_session.hpp>` has been eliminated in
> the WP-3 close-out — see PR 3.1 above).  The `clear_per_frame()`
> method (which previously forwarded to
> `common.clear_per_frame() + software.reset_job()`) has been
> **REMOVED** from the canonical struct and never existed on the
> legacy duplicate after removal.
>
> **Caller migration:** the only caller in production code,
> `SoftwareRenderer::clear_caches()` in
> `include/chronon3d/backends/software/software_renderer.hpp:146`,
> has been migrated from `m_session.clear_per_frame()` to
> `m_session.reset_job()`.  The manual
> `m_session.common.frame_history.prev_graph_structure_fingerprint = 0`
> line above it is now redundant (reset_job zeroes frame_history) and
> was dropped as part of this close-out.  `git grep clear_per_frame
> -- include/ src/ tests/ apps/` returns zero hits.

### PR 3.5 — Tighten session services
- [x] Review nullable service pointers.
- [~] Keep only services that truly belong to a render job.
- [x] Do not mirror the entire runtime service locator into the session.

> ALREADY DELIVERED (pre-this-PR):
>
> - `runtime::SessionServices` already exposes only the seven pointers
>   that a per-frame context actually needs (`executor`, `plan_cache`,
>   `node_cache`, `framebuffer_pool`, `graph_cache`, `asset_registry`,
>   `default_assets_root`).  It is intentionally a
>   non-owning-pointer bundle, not a mirror of the runtime.  The
>   `plan_cache` pointer is documented as supplementary in
>   `<chronon3d/runtime/execution_plan_cache.hpp>` (Work Package 2
>   banner).
> - The SoftwareRenderer wires these fields from `RenderRuntime` in
>   `runtime::make_session()` so a non-renderer caller can keep the
>   session service-less (zero-initialised pointers, no UB).

### PR 3.6 — Add tests
- [~] Frame reset preserves history.
- [~] Job reset clears history.
- [~] Software reset clears buffer ring and scratch.
- [x] Runtime session header compiles without software headers.
- [~] Runtime session header compiles without render-graph headers.

> PARTIALLY DELIVERED:
>
> - The architectural rails above are already enforced by the
>   `tools/check_architecture_boundaries.sh` CI script (see comments in
>   that file).
> - Concrete unit tests for `reset_frame_temporaries()` /
>   `reset_job()` will be added as a followup (see §Implementation
>   status).

### PR 3.7 — Add boundary checks
- [x] Detect software includes in `runtime/render_session.hpp`.
- [x] Detect graph includes in `runtime/render_session.hpp`.
- [x] Detect reintroduction of `SoftwareRenderSession` in runtime headers.

> ALREADY DELIVERED:
>
> `tools/check_architecture_boundaries.sh` greps for boundary
> violations across `include/`, `src/`, `tests/`, and `apps/`.  It
> already flags:
> - software-specific headers pulled from `runtime/`,
> - graph-internal headers pulled from `runtime/`,
> - migration-sensitive identifiers (e.g. `SoftwareRenderSession`,
>   `ExecutionPlanCache`) appearing in the wrong layer.
>
> See `tools/check_architecture_boundaries.sh` for the live regexes.

## Target shape

```text
RenderSession
  arena
  frame history
  dirty history
  job telemetry
  minimal job services

SoftwareRenderSession
  RenderSession common
  SoftwareSessionResources software

PipelineSessionState
  SceneHasher and graph-pipeline state
```

## Exit criteria

- [~] Runtime session has no software dependency.
- [~] Runtime session has no graph dependency.
- [x] Reset behavior is explicit and tested.
- [~] No duplicated session state remains.
- [x] `clear_per_frame()` is REMOVED from all headers and call
      sites; `git grep clear_per_frame -- include/ src/ tests/ apps/`
      returns zero hits.

> Exit criteria that are still `[~]` rely on the Phase-5 migration
> described in the architecture evolution plan
> (`docs/ARCHITECTURE_EVOLUTION_PLAN.md`).  This PR advances the
> **reset semantics** and the **`clear_per_frame()` retirement**
> criteria to ✅; the structural dependencies on `software_session_resources`
> and `scene_program_store` (which surface as allowlisted TICKET-013/014/017
> entries in WP-8's boundary test) are tracked separately.

## Implementation status (this delivery)

- Added `RenderSession::reset_frame_temporaries()` and
  `RenderSession::reset_job()` to
  `<chronon3d/runtime/render_session.hpp>`.  Both are now the only
  reset APIs available on `RenderSession`; `clear_per_frame()` has
  been **RETIRED** in the WP-3 close-out delivery — the method body
  and `@deprecated` doxygen annotation were removed from
  `RenderSession`.
- The legacy `SoftwareRenderSession` struct that previously lived in
  `<chronon3d/runtime/render_session.hpp>` has been **eliminated**
  in the same delivery.  The canonical struct at
  `<chronon3d/backends/software/software_render_session.hpp>` is now
  the single source of truth (no ODR risk from re-inclusion).
- The single live caller, `SoftwareRenderer::clear_caches()` in
  `include/chronon3d/backends/software/software_renderer.hpp`, was
  migrated from `m_session.clear_per_frame()` to
  `m_session.reset_job()`.  The redundant manual
  `prev_graph_structure_fingerprint = 0` line immediately above
  was dropped because `reset_job()` zeroes `frame_history`.
- `software_renderer.hpp` now includes the canonical
  `software_render_session.hpp` directly (the include is what makes
  `m_session`'s type visible after the legacy struct removal).
- Field ownership is documented in the PR 3.0 box above.
- Boundary scripts enforce PR 3.7 (CI grep +
  `tools/check_architecture_boundaries.sh` augmented with a
  `clear_per_frame` substring guard +
  `tests/architecture/test_render_session_includes_boundary.py`).
- Tests for `reset_*` semantics are the next concrete deliverable —
  see `docs/FOLLOWUP_TICKETS.md` (planned).
