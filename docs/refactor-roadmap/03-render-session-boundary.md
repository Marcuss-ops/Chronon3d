# Work Package 3 — Remaining session isolation work

## Current state

`SoftwareRenderSession` is separated from the runtime header, `clear_per_frame()` is retired, and explicit frame/job reset methods exist.

## TODO

### PR 3.0 — Fix throwing accessors

Files:
- `include/chronon3d/runtime/render_session.hpp`
- `src/runtime/render_session.cpp`
- `include/chronon3d/runtime/render_runtime.hpp`
- `src/runtime/render_runtime.cpp`

Actions:
- [ ] Remove `noexcept` from accessors that throw.
- [ ] Alternatively make the invariant non-throwing with assertions and guaranteed wiring.
- [ ] Add tests for default-constructed sessions and unattached backends.
- [ ] Prevent `std::terminate` from a throw escaping a `noexcept` function.

### PR 3.1 — Restore job isolation

Current problem: `SceneHasher` and `SceneProgramStore` are runtime-owned and shared by all sessions from one runtime.

Actions:
- [ ] Move mutable scene-hashing state to a job/pipeline-session owner.
- [ ] Move mutable `SceneProgramStore` ownership to the render job/session.
- [ ] Keep `runtime/render_session.hpp` graph-header-free through PIMPL or an opaque session-state implementation.
- [ ] Ensure `reset_job()` never clears state belonging to another session.
- [ ] Keep engine-lifetime configuration on `RenderRuntime`, not mutable job cache entries.

### PR 3.2 — Finish history consolidation

- [ ] Replace `RendererFrameHistory` with canonical `FrameHistory` at call sites.
- [ ] Replace `RendererDirtyTelemetry` with canonical `DirtyHistory` or a clearly named telemetry type.
- [ ] Remove `RendererLayerHistory` after folding compatible layer bbox state into `DirtyHistory`.
- [ ] Keep one owner for previous-frame and previous-layer state.

### PR 3.3 — Add reset and isolation tests

- [ ] `reset_frame_temporaries()` preserves frame and layer history.
- [ ] `reset_job()` clears only the current job history and caches.
- [ ] Software reset clears scratch and buffer ring.
- [ ] Two sessions from one runtime do not clear each other.
- [ ] Two concurrent sessions do not share scene-hasher mutation.
- [ ] Runtime session header compiles without graph or software includes.

### PR 3.4 — Tighten session services

- [ ] Remove service pointers that do not belong to a render job.
- [ ] Replace nullable production dependencies with validated construction where practical.
- [ ] Keep the service table as a narrow non-owning view, not a second runtime locator.

## Exit criteria

- [ ] No throwing function is incorrectly marked `noexcept`.
- [ ] Mutable job state is isolated per session.
- [ ] Reset methods cannot affect another render job.
- [ ] Legacy renderer-prefixed history aliases are removed from production call sites.
- [ ] Reset and multi-session isolation tests pass.
