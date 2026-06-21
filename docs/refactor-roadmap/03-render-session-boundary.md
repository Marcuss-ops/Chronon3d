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
- [x] Remove `noexcept` from accessors that throw.  The four `[[nodiscard]]` `noexcept` annotations on `RenderSession::scene_hasher()` (mutable + const overloads) and `program_store()` (mutable + const overloads) were dropped in `include/chronon3d/runtime/render_session.hpp`.  Their bodies in `src/runtime/render_session.cpp` throw `std::runtime_error` when the session's `services.scene_hasher` / `services.program_store` pointers are null; a throw unwinding through a `noexcept` function invokes `std::terminate`.  Removing the specifier lets the throw propagate to the caller.  `arena()` accessors REMAIN `noexcept` because their bodies never throw.
- [x] Prevent `std::terminate` from a throw escaping a `noexcept` function.  Follows from the noexcept-removal action above; verified by `grep -nE 'noexcept' include/chronon3d/runtime/render_session.hpp` returning only the `arena()` lines (the four `scene_hasher` / `program_store` declarations in question are gone from the noexcept set).
- [~] Add tests for default-constructed sessions and unattached backends — delivered in PR 3.3 alongside the broader reset / isolation coverage so the diagnostic and the test lattice live in one place.  PR 3.0 keeps the noexcept fix isolated to the header; the test file is registered with the lattice in PR 3.3.
- [ ] (alternative) Make the invariant non-throwing with assertions and guaranteed wiring — explicitly NOT taken.  See rationale below.

#### PR 3.0 — why we kept the throw instead of switching to assertions / abort

The throw path was kept (vs. an assertion + `std::abort` or a silent null return) so callers receive actionable error information at the call site.  Production routes reach these accessors only via `make_session()`, which wires `services.scene_hasher` and `services.program_store` to runtime-owned pointers; the throw branch exists solely as a defensive guard for default-constructed sessions (test fixtures, future ad-hoc session holders).  An assertion-driven `abort()` would convert a recoverable wiring error into a process kill; a silent null return would convert it into UB at the dereference.  The throw lets the error surface and the caller decide.

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

- [x] No throwing function is incorrectly marked `noexcept`.  (PR 3.0 dropped `noexcept` from the four `RenderSession::scene_hasher` / `program_store` accessor signatures whose bodies throw on default-constructed sessions.)
- [ ] Mutable job state is isolated per session.
- [ ] Reset methods cannot affect another render job.
- [ ] Legacy renderer-prefixed history aliases are removed from production call sites.
- [ ] Reset and multi-session isolation tests pass.
