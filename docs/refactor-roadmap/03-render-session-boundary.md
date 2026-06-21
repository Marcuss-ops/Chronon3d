# Work Package 3 — Render session boundary

## Goal

Make `runtime/render_session.hpp` backend-agnostic and free from render-graph implementation types.

## TODO

### PR 3.0 — Map current ownership
- [ ] List every `RenderSession` field and every caller.
- [ ] Mark each field as runtime, graph-pipeline, or software-backend state.

### PR 3.1 — Split software session types
- [ ] Move `SoftwareRenderSession` to `backends/software/software_render_session.hpp`.
- [ ] Keep `SoftwareSessionResources` under the software backend.
- [ ] Update includes and call sites.

### PR 3.2 — Move graph-pipeline state
- [ ] Move `SceneHasher` out of `RenderSession`.
- [ ] Create or reuse a pipeline-session state type.
- [ ] Keep graph-only state inside the graph pipeline module.

### PR 3.3 — Consolidate history types
- [ ] Replace legacy renderer-prefixed history aliases with runtime names.
- [ ] Fold layer bounding-box history into `DirtyHistory` when compatible.
- [ ] Keep one owner for each history value.

### PR 3.4 — Define reset semantics
- [ ] Add `reset_frame_temporaries()`.
- [ ] Add `reset_job()`.
- [ ] Frame reset clears temporary memory and frame telemetry only.
- [ ] Job reset also clears history, caches owned by the session, and software resources.
- [ ] Retire ambiguous `clear_per_frame()` after call sites migrate.

### PR 3.5 — Tighten session services
- [ ] Review nullable service pointers.
- [ ] Keep only services that truly belong to a render job.
- [ ] Do not mirror the entire runtime service locator into the session.

### PR 3.6 — Add tests
- [ ] Frame reset preserves history.
- [ ] Job reset clears history.
- [ ] Software reset clears buffer ring and scratch.
- [ ] Runtime session header compiles without software headers.
- [ ] Runtime session header compiles without render-graph headers.

### PR 3.7 — Add boundary checks
- [ ] Detect software includes in `runtime/render_session.hpp`.
- [ ] Detect graph includes in `runtime/render_session.hpp`.
- [ ] Detect reintroduction of `SoftwareRenderSession` in runtime headers.

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

- [ ] Runtime session has no software dependency.
- [ ] Runtime session has no graph dependency.
- [ ] Reset behavior is explicit and tested.
- [ ] No duplicated session state remains.
