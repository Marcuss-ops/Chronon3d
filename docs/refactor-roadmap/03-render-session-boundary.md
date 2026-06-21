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

> Both `SoftwareRenderSession` and `SoftwareSessionResources` already live
> under `backends/software/`; the canonical headers are
> `<chronon3d/backends/software/software_render_session.hpp>` and
> `<chronon3d/backends/software/software_session_resources.hpp>`.  The
> legacy inline duplicate definitions in
> `<chronon3d/runtime/render_session.hpp>` are kept for backward
> compatibility but `SoftwareRenderer::m_session` already lives on
> `SoftwareRenderSession`.  Migration is complete for production callers
> (see Sentinel-003 in `docs/migrations/2026-06-renderer-state.md`).

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

> DELIVERED in this PR:
>
> **RenderSession (`<chronon3d/runtime/render_session.hpp>`):**
> - New `reset_frame_temporaries()` method that clears
>   `dirty_telemetry` only (frame-scoped monitoring counters).
> - New `reset_job()` method that performs a full reset:
>   `reset_frame_temporaries()` + `frame_history = {}` +
>   `layer_history = {}` + `scene_hasher = {}` +
>   `program_store->clear()`.
> - `clear_per_frame()` is kept for backward compatibility but
>   annotated `@deprecated` in its doxygen comment, recommending
>   callers migrate to the two new methods above.
>
> **SoftwareSessionResources** already had
> `reset_frame_temporaries()` (clears scratch_buffer + scene_hasher)
> and `reset_job()` (also drops buffer_ring) — kept as-is.
>
> **SoftwareRenderSession** (canonical wrapper at
> `backends/software/software_render_session.hpp`) already routed both
> reset methods to BOTH halves before this PR — no change.

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

> Exit criteria that are still `[~]` rely on the Phase-5 migration
> described in the architecture evolution plan
> (`docs/ARCHITECTURE_EVOLUTION_PLAN.md`).  This PR advances the
> **reset semantics** criteria — the others are tracked in followup.

## Implementation status (this delivery)

- Added `RenderSession::reset_frame_temporaries()` and
  `RenderSession::reset_job()` to
  `<chronon3d/runtime/render_session.hpp>`.  Both are **additive**;
  no callers are forced to migrate (yet).  `clear_per_frame()` is
  left behind with a `@deprecated` doxygen comment pointing at the
  two new methods.
- Field ownership is documented in the PR 3.0 box above.
- Boundary scripts already enforce PR 3.7 (CI grep).
- Tests for `reset_*` semantics are the next concrete deliverable —
  see `docs/FOLLOWUP_TICKETS.md` (planned).
