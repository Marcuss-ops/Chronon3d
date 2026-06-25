# Work Package 3 — Remaining session-boundary work

## Current state

Completed:

- `SoftwareRenderSession` has one canonical backend header.
- `clear_per_frame()` is retired.
- `reset_frame_temporaries()` and `reset_job()` are explicit.
- `SceneHasher` and `SceneProgramStore` are isolated per session.
- `FrameHistory`, `DirtyHistory`, and layer bbox history are canonicalized.
- Multi-session reset/isolation tests exist.

The remaining issue is dependency direction: `runtime/render_session.hpp` directly includes render-graph cache and hashing implementation headers. Job ownership is correct, but the runtime layer now depends on graph implementation details.

## TODO

### PR 3.0 — Introduce opaque per-session graph state

Preferred shape:

```cpp
class RenderSessionGraphState;

struct RenderSession {
    std::unique_ptr<RenderSessionGraphState> graph_state;
};
```

Actions:

- [ ] Remove direct includes of `scene_program_store.hpp` and `scene_hasher.hpp` from `runtime/render_session.hpp`.
- [ ] Define graph-state storage in a `.cpp` or internal graph/pipeline header.
- [ ] Preserve per-session ownership and eager/default-safe construction.
- [ ] Preserve `scene_hasher()` and `program_store()` access through narrow methods or a typed internal service.
- [ ] Keep destruction and move operations valid with the incomplete type.

### PR 3.1 — Fix remaining throw/noexcept contracts

- [ ] Remove `noexcept` from `RenderRuntime::backend()` and its const overload while they throw before backend attachment.
- [ ] Alternatively make backend attachment a construction invariant and use a non-throwing assertion contract.
- [ ] Add a test for backend access before attachment.
- [ ] Keep genuinely non-throwing session accessors marked `noexcept`.

### PR 3.2 — Tighten session services

- [ ] Add an explicit asset-resolver pointer when session code needs path resolution.
- [ ] Remove `default_assets_root` string back-pointers after WP-8 resolver migration.
- [ ] Keep only runtime-owned services required by one render job.
- [ ] Do not reintroduce scene hasher/program store pointers into `SessionServices`.
- [ ] Validate required production services during session creation.

### PR 3.3 — Preserve reset and isolation coverage after PIMPL

- [ ] `reset_frame_temporaries()` preserves frame and layer history.
- [ ] `reset_job()` clears only the current job.
- [ ] Two sessions from one runtime keep distinct graph state.
- [ ] Default-constructed session graph state remains valid.
- [ ] Software scratch and buffer-ring reset semantics remain covered.
- [ ] Runtime session public header compiles without render-graph implementation headers.

### PR 3.4 — Add a permanent dependency guard

- [ ] Fail if `runtime/render_session.hpp` includes `render_graph/cache/*`.
- [ ] Fail if it includes `render_graph/core/scene_hasher.hpp`.
- [ ] Allow forward declarations and opaque internal implementation only.

## Exit criteria

- [ ] Job graph state remains isolated per session.
- [ ] Runtime session public header does not expose graph implementation headers.
- [ ] No throwing accessor is incorrectly marked `noexcept`.
- [ ] Session services are narrow and explicit.
- [ ] Reset and multi-session tests pass after the boundary change.
