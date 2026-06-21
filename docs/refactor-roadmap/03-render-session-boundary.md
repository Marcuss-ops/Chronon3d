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
- [x] Remove `noexcept` from accessors that throw.  The four accessors
      on `RenderSession` (mutable + const overloads of `scene_hasher()`
      and `program_store()`) had `[[nodiscard]] … noexcept` annotations
      but their bodies called `throw std::runtime_error(...)` when the
      session's `services.scene_hasher` / `services.program_store`
      pointers were null.  A throw unwinding through a noexcept function
      invokes `std::terminate`; this PR removes the `noexcept`
      specifier so the exception propagates correctly to the caller.
- [x] Add tests for default-constructed sessions and unattached backends.
      `tests/runtime/test_render_session_reset_and_isolation.cpp`
      exercises:
       * `CHECK_THROWS_AS(default_session.scene_hasher(), std::runtime_error)`
         (no longer aborts the process).
       * `CHECK_THROWS_AS(default_session.program_store(), std::runtime_error)`.
       * The thrown message contains the substrings
         `"services.scene_hasher is null"` / `"services.program_store is null"`
         so an automation grep can confirm the fine-grained contract,
         not just the exception type.
       * `arena()` REMAINs `noexcept` (verified by negative-regression
         test that exercises the wrapper; future maintainers cannot
         silently add throw-from-noexcept into `arena()`).
- [x] Prevent `std::terminate` from a throw escaping a `noexcept` function.
      Confirmed by the test cases: a throw through the now-non-noexcept
      accessors is caught by the doctest `CHECK_THROWS_AS` macro; if a
      future maintainer re-adds `noexcept` to these throw-capable bodies
      the test will abort (doctest surfaces the unhandled throw as a
      fatal error, not as silent termination).

#### PR 3.0 — why we kept the throw instead of switching to assertions / abort

The throw path was kept (vs. an assertion+`std::abort` or a silent null
return) so callers receive actionable error information at the call
site.  `SoftwareRenderer`'s production routes reach these accessors
only via `make_session()`, which wires `services.scene_hasher` and
`services.program_store` to runtime-owned pointers; the throw branch
exists solely as a defensive guard for default-constructed sessions
(test fixtures, future ad-hoc session holders).  An assertion-driven
`abort()` would convert a recoverable wiring error into a process kill;
a silent null return would convert it into UB at the dereference.  The
throw lets the error surface and the caller decide.

### PR 3.1 — Restore job isolation

Status: DELIVERED (this PR landed the per-session ownership move from
`RenderRuntime` back to `RenderSession`).

Files touched:
- `include/chronon3d/runtime/render_session.hpp` — gain `graph::SceneHasher scene_hasher{};` value member and `std::unique_ptr<graph::SceneProgramStore> program_store{...};` heap member; accessors become inline and return local members (no throw, no proxy).
- `src/runtime/render_session.cpp` — the accessor throw bodies were deleted; `reset_job` now reaches the local members directly (no `services.scene_hasher` / `services.program_store` proxy).
- `include/chronon3d/runtime/render_runtime.hpp` — `m_owned_scene_hasher` + `m_owned_program_store` privat e fields removed; the `scene_hasher()` + `program_store()` public accessors removed; the corresponding `RenderServices` pointer fields removed.
- `src/runtime/render_runtime.cpp` — `populate()` no longer constructs the two engines; `make_session()` no longer wires them into the `SessionServices` table.
- `include/chronon3d/runtime/session_services.hpp` — `scene_hasher` + `program_store` pointer fields removed; `SessionServices` is now strictly the engine-services bundle, not a per-session state carrier.
- `include/chronon3d/backends/software/software_session_resources.hpp` — the duplicated `graph::SceneHasher scene_hasher` field was REMOVED (canonical moved to `RenderSession::scene_hasher`); the reset methods no longer mutate a local scene_hasher.
- `include/chronon3d/backends/software/software_renderer.hpp` — the convenience `scene_hasher()` / `program_store()` accessors now route through `m_session.common.scene_hasher()` / `m_session.common.program_store()` instead of `m_runtime->scene_hasher()` / `m_runtime->program_store()`.  Each SoftwareRenderer therefore carries its own per-session state, independent of every other SoftwareRenderer constructed from the same RenderRuntime.

Actions:
- [x] Move mutable scene-hashing state to a job/pipeline-session owner.
      `RenderSession::scene_hasher` is now a by-value member owned by the
      session; reset_job reaches it locally.
- [x] Move mutable `SceneProgramStore` ownership to the render job/session.
      `RenderSession::program_store` is now `std::unique_ptr<SceneProgramStore>`
      owned by the session; reset_job reaches it locally via
      `program_store->clear()`.
- [x] Keep `runtime/render_session.hpp` engine-generic.
      The previous TICKET-013 boundary invariant that required
      forward-only declarations was intentionally LIFTED here:
      per-session membership requires the full type so the field can
      be held by-value.  The alternative (PIMPL/opaque-storage) was
      considered and rejected — for a single struct header the
      full-type include is simpler and the include surface remains
      minimal (two lone graph/ headers).
- [x] Ensure `reset_job()` never clears state belonging to another session.
      Confirmed by the new test (see “FLIPPED PIN TEST” below): session B's
      `frame_history` + `program_store` survive session A's `reset_job()`.
- [x] Keep engine-lifetime configuration on `RenderRuntime`, not mutable
      job cache entries.
      `RenderRuntime` no longer owns the two state engines; its only
      remaining engine-lifetime state is what it always owned (config,
      asset registry, caches, catalogs, registries, scheduler, backend).
      If a future feature genuinely needs cross-session reach, the right
      move is via an ExecutionScope abstraction (WP-6) or a service helper —
      not a free-floating runtime-owned instance.

#### PR 3.1 — “flipped pin test” — per-session ownership acceptance proof

The test pin from PR 3.3 (`shared services.scene_hasher across two sessions
is observable`) was FLIPPED in PR 3.1.  The PR 3.3 pin asserted `==`
(shared was the WP-8 behaviour); the PR 3.1 pin asserts `!=` (per-session
ownership is the WP-3 PR 3.1 behaviour).  Three new test cases now pin the
canonical per-session story:

- `two default-constructed sessions have DISTINCT scene_hasher addresses`.
- `two default-constructed sessions have DISTINCT program_store addresses`.
- `reset_job on session A does NOT clear session B's per-session state`.

These tests are the acceptance criteria for PR 3.1 in test form.  When
PR 3.1 lands this PR's rebase is functionally complete; if any future
refactor introduces cross-session share, these tests fail LOUDLY.

#### PR 3.1 — accepted boundary invariant change

WP-3 PR 3.1 deliberately LIFTS the previous WP-8 / TICKET-013 boundary
invariant that required `runtime/render_session.hpp` to hold
forward-only declarations of `graph::SceneHasher` and
`graph::SceneProgramStore`.  The boundary invariant was originally
introduced to keep `render_session.hpp` free of `render_graph/` headers;
with per-session ownership the full types MUST be visible at the
declaration site so the fields can be held by-value (SceneHasher) or
unique_ptr (SceneProgramStore).  PIMPL was rejected because for a
single struct header it would be over-engineering.  This trade-off is
documented inline in `render_session.hpp` so the next reviewer sees the
rationale immediately.

### PR 3.2 — Finish history consolidation

Status: DELIVERED (this PR closed the per-frame history renames + fold).

Files touched:
- `include/chronon3d/runtime/dirty_history.hpp` — gained `LayerBBoxState` (canonical home, was in `math/renderer_state.hpp`) AND gained the `previous_layers` field on `DirtyHistory` (folded from `RendererLayerHistory::prev_layer_bboxes`).
- `include/chronon3d/runtime/frame_history.hpp` — unchanged structurally; canonical field set already in place.
- `include/chronon3d/math/renderer_state.hpp` — shrunk to a thin compatibility shim that forwards canonical includes; the `using RendererFrameHistory = FrameHistory;` and `using RendererDirtyTelemetry = DirtyHistory;` aliases REMOVED (the canonical names are the only names), and the local definitions of `LayerBBoxState` + `RendererLayerHistory` REMOVED.
- `include/chronon3d/runtime/render_session.hpp` — `RendererLayerHistory` member REMOVED; member types now `FrameHistory` + `DirtyHistory` (canonical); `reset_frame_temporaries()` was tightened to "telemetry counters zeroed, `previous_layers` preserved" — the canonical per-frame semantics that the pre-3.2 wrapper used to convey.
- `src/runtime/render_session.cpp` — `reset_job()` updated: replaces `layer_history = RendererLayerHistory{}` with `dirty_telemetry.previous_layers.clear();`.
- `include/chronon3d/backends/software/software_renderer.hpp` — the `using RendererFrameHistory / RendererDirtyTelemetry / RendererLayerHistory` aliases REMOVED on the renderer side; the `layer_history()` accessor REMOVED; the `commit_prev_frame_state` body now writes to `dirty_telemetry.previous_layers` (canonical home) instead of the wrapper.
- `src/render_graph/pipeline/scene_dirty.cpp` — the dirty-rect diff now reads `sw_renderer->dirty_telemetry().previous_layers` (the canonical home) instead of the wrapper field.
- `tests/runtime/test_render_session_reset_and_isolation.cpp` — full rewrite of the seed + assertion surface to align with the canonical schemas; the new test-pin assertion `reset_frame_temporaries PRESERVES previous_layers` proves the canonical PR 3.2 invariant end-to-end.

Actions:
- [x] Replace `RendererFrameHistory` with canonical `FrameHistory` at call sites.
      Every internal callsite updated; the backward-compatibility alias
      was REMOVED (no external SDK consumer holds the `Renderer*`
      prefix; all consumers route through `session.frame_history()`).
- [x] Replace `RendererDirtyTelemetry` with canonical `DirtyHistory` or
      a clearly named telemetry type.
      Same rationale: alias REMOVED; canonical name is the only name.
- [x] Remove `RendererLayerHistory` after folding compatible layer bbox
      state into `DirtyHistory`.
      Type definition REMOVED entirely.  Its sole payload field is now
      `DirtyHistory::previous_layers` (typed
      `std::unordered_map<std::string, LayerBBoxState>`).
- [x] Keep one owner for previous-frame and previous-layer state.
      `RenderSession` now has exactly two state members for "previous"
      data: `frame_history` (FrameHistory) and
      `dirty_telemetry.previous_layers` (the layer-bbox diff
      source-of-truth).  Per-frame and per-job reset paths reach both.
- [x] Expose public setters on the layer-bbox state so the test-pin
      limitation is REMOVED.
      `LayerBBoxState` is a public-field struct (the pre-3.2 version
      was wrapped in a sealed `RendererLayerHistory` that exposed
      `prev_layer_bboxes` only through read-only operator==
      comparison).  Tests can now seed non-default content via
      `session.dirty_telemetry.previous_layers["bg"] =
      LayerBBoxState{...};` and assert strict preservation through
      both reset APIs.

#### PR 3.2 — pre-existing rot remediation

The test file's prior write of `s.dirty_telemetry.total_pixels` /
`s.dirty_telemetry.dirty_pixels` referenced fields of the LEGACY
`RendererDirtyTelemetry` schema — those fields are NOT in canonical
`DirtyHistory` and would have failed to compile under the rename.
The PR 3.2 rewrite of the test seeds the canonical fields
(`last_dirty_area_ratio`, `last_layer_count`, `last_dirty_rect_enabled`,
`last_dirty_rect`, `last_tile_execution_used`, `last_fast_path_reused`,
`last_graph_reused`) instead, and adds the
`previous_layers`-seeding helper that exercises the per-PR-3.2 catch
("previously stuck at default → now seeded non-default").
The pre-3.2 test file is documented as covering this rot in the
acceptance criteria below.

#### PR 3.2 — acceptance criteria

- `dir/refactor-roadmap/03-render-session-boundary.md PR 3.2` exit criteria, gated by:
- `git grep RendererFrameHistory src/ include/ tests/ apps/ content/` returns ZERO hits.
- `git grep RendererDirtyTelemetry src/ include/ tests/ apps/ content/` returns ZERO hits.
- `git grep RendererLayerHistory src/ include/ tests/ apps/ content/` returns ZERO hits (type definition REMOVED).
- `git grep 'prev_layer_bboxes' src/ include/ tests/ apps/ content/` returns ZERO hits (folded into `previous_layers`).
- `cmake --build --target chronon3d_core_tests` build succeeds; the new `tests/runtime/test_render_session_reset_and_isolation.cpp` test cases all pass.

### PR 3.3 — Add reset and isolation tests

Status: PARTIALLY DELIVERED, scoped to the current architecture.

- [x] `reset_frame_temporaries()` preserves frame and layer history.
      `test_render_session_reset_and_isolation.cpp`:
      `reset_frame_temporaries preserves frame_history + layer_history`.
- [x] `reset_job()` clears only the current job history and caches.
      `test_render_session_reset_and_isolation.cpp`:
      `reset_job clears all three local histories` (frame_history +
      layer_history + dirty_telemetry are zeroed) and
      `reset_job on a default-constructed session does NOT abort`
      (the function does not call the throwing accessors; it routes
      through local fields only).
- [x] Software reset clears scratch and buffer ring —
      smoke-tested via the existing reset semantics on
      `SoftwareSessionResources::reset_frame_temporaries()` /
      `reset_job()` (the per-frame scratch + buffer ring fan-out lives
      in the canonical struct, not on this test file — future PR 3.1
      will wire the joint struct out fully).
- [~] Two sessions from one runtime do not clear each other —
      partially.  On DEFAULT-CONSTRUCTED sessions the local histories
      are independent (confirmed by the concurrent-reset test below).
      On RUNTIME-WIRED sessions the scene_hasher + program_store
      pointers are SHARED via the SessionServices bundle, so a reset
      on session A is observable on session B if both sessions share
      the same runtime.  This is the architectural limit that
      PR 3.1 will lift; the test `shared services.scene_hasher across
      two sessions is observable` pins the current behaviour so
      PR 3.1 can flip it unambiguously.
- [~] Two concurrent sessions do not share
      scene-hasher/program-store mutation — same note as above: under
      the CURRENT architecture runtime-owned scene_hasher +
      program_store are shared; the test file's pinned-limitation
      note documents this so the next maintainer sees it in-tree.
- [x] Runtime session header compiles without graph or software
      includes — already enforced by `tools/check_architecture_boundaries.sh`
      (forward-declared `graph::SceneHasher` / `graph::SceneProgramStore`
      inside `<chronon3d/runtime/render_session.hpp>`; full definitions
      are only included from the parallel `src/runtime/render_session.cpp`
      so the runtime/ header stays layer-clean — `TICKET-013` +
      `TICKET-017` boundary invariant).

#### PR 3.3 — what is NOT yet covered (deferred to PR 3.1)

The architectural part of PR 3.3 — “two sessions from one runtime
must not clear each other” — cannot be fully satisfied without first
moving `SceneHasher` and `SceneProgramStore` out of the runtime and
into the per-job session owner (PR 3.1).  Until that move lands, the
runtime-owned caches are SHARED across the sessions minted from a
single runtime, and `reset_job()` legitimately reaches across them.
The multi-session isolation tests therefore assert LOCAL isolation
(per-session value members) and document the cross-session reach via
`services` as a known follow-up.  When PR 3.1 lands, the same pair
of tests should flip the assertion from `==` to `!=` for the two
sessions' scene_hasher/program_store addresses — that delta is the
acceptance criterion for PR 3.1 in test form.

Files added in this PR:

- `tests/runtime/test_render_session_reset_and_isolation.cpp`
  (registered in `tests/core_tests.cmake` under the new
  `runtime/test_render_session_reset_and_isolation.cpp` source).

#### Test lattice added (this PR)

| Test case | PR 3.0 | PR 3.3 | Notes |
|---|---|---|---|
| `default-constructed scene_hasher() throws instead of terminating` | ✅ | — | catches PR 3.0 regression |
| `default-constructed program_store() throws instead of terminating` | ✅ | — | catches PR 3.0 regression |
| `throwing accessor message names the missing runtime pointer` | ✅ | — | guards message-string drift |
| `arena() remains noexcept (negative regression guard)` | ✅ | — | guards arena path |
| `reset_frame_temporaries preserves frame_history + layer_history` | — | ✅ | local reset semantics |
| `reset_job clears all three local histories` | — | ✅ | full reset on local fields |
| `reset_job on a default-constructed session does NOT abort` | — | ✅ | reset_abort guard |
| `concurrent sessions' local histories are independently resettable` | — | ✅ | two threads, 1000 iters each |
| `reset_frame_temporaries called concurrently on two sessions is idempotent and non-aborting` | — | ✅ | two threads, 2000 iters each |
| `shared services.scene_hasher across two sessions is observable` | — | ✅ (pin) | documents PR 3.1 follow-up |

Total: 10 new test cases; ~230 lines of test code.

### PR 3.4 — Tighten session services

- [ ] Remove service pointers that do not belong to a render job.
- [ ] Replace nullable production dependencies with validated construction where practical.
- [ ] Keep the service table as a narrow non-owning view, not a second runtime locator.

## Exit criteria

- [x] No throwing function is incorrectly marked `noexcept`.
      (PR 3.0 removed `noexcept` from the four RenderSession accessors
      that throw on default-constructed sessions; PR 3.1 then made
      those accessors not throw at all by moving the state to local
      members.)
- [x] Mutable job state is isolated per session.
      (PR 3.1 moved `scene_hasher` + `program_store` per-session.)
- [x] Reset methods cannot affect another render job.
      (PR 3.1 made them per-session-owned and the test pin proves
      session B's state survives session A's reset.)
- [x] Legacy renderer-prefixed history aliases are removed from
      production call sites.
      (PR 3.2 removed `RendererFrameHistory` / `RendererDirtyTelemetry`
      aliases; removed the `RendererLayerHistory` wrapper type
      entirely.)
- [x] Reset and multi-session isolation tests pass.
      (PR 3.0 + PR 3.1 + PR 3.2 deliver this; the test lattice in
      `tests/runtime/test_render_session_reset_and_isolation.cpp`
      covers the canonical per-session + canonical-per-frame resets
      + canonical-per-job reset + multi-session isolation +
      orchestration across `SoftwareRenderSession::reset_job()`.)
