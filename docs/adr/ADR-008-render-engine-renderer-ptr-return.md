# ADR-008 — `RenderEngine::renderer()` returns pointer (ref→ptr API break)

| Field | Value |
|---|---|
| **Status** | Accepted |
| **Date** | 2026-06-22 |
| **Deciders** | `codex/agent1-renderer-boundary` author, code-reviewer-minimax-m3 |
| **Tags** | `api-break`, `boundary-gate`, `sdk`, `software-renderer` |
| **Related** | [ADR-002 — RenderRuntime Ownership](./ADR-002-render-runtime-ownership.md), [docs/stabilization-plan/04-cmake-module-registry.md](../stabilization-plan/04-cmake-module-registry.md) |

## Context

The 06 R3b boundary refactor on branch `codex/agent1-renderer-boundary`
restored the architectural line between the per-instance orchestrator
`SoftwareRenderer` and the polymorphic graph backend
`graph::RenderBackend`.  Concretely:

* The boundary gate `tools/check_software_renderer_boundary.sh` enforces
  five invariants on the `SoftwareRenderer` public surface.  **Invariant
  I1** forbids double inheritance: `SoftwareRenderer` MUST NOT extend
  `graph::RenderBackend` *and* `Renderer` simultaneously.  The fix is a
  single-identity inheritance (`class SoftwareRenderer : public Renderer`).

* In the previous design, `SoftwareRenderer` extended `graph::RenderBackend`,
  and a caller that held a `RenderBackend&` could pass a `SoftwareRenderer`
  implicitly (IS-A upcast).  After I1, that upcast is no longer valid.

* `RenderEngine::renderer()` was the public SDK accessor that returned the
  underlying implementation by reference so callers could reach
  `RenderBackend` semantics through the engine facade (e.g.
  `engine.renderer().counters()`, `engine.renderer().render_frame(...)`).
  With the IS-A relationship removed, the return type MUST change from
  `SoftwareRenderer&` to `SoftwareRenderer*` — a *runtime* pointer
  rather than a *type* reference — because callers need to re-route
  through the typed `backend()` accessor instead of relying on the
  implicit base-class upcast.

The change is observable at:

* `include/chronon3d/api/render_engine.hpp` — declaration:
  ```cpp
  [[nodiscard]] SoftwareRenderer* renderer() noexcept;
  [[nodiscard]] const SoftwareRenderer* renderer() const noexcept;
  ```

* `include/chronon3d/runtime/render_pipeline.hpp` — `RenderPipeline`'s
  constructor signature changed in lockstep, because the pipeline took
  the same per-instance reference datum by reference.

## Decision

1. **`RenderEngine::renderer()` returns `SoftwareRenderer*` (raw owning-of-lifetime
   pointer), not `SoftwareRenderer&`.** Both const and non-const overloads
   inherit `noexcept`; a null return is impossible because the engine
   constructs the renderer before the first queryable state.  Internal
   use sites dereference unconditionally.

2. **Callers that need the polymorphic graph backend MUST now route through
   `sw_renderer.backend()`** (a per-renderer forwarder into the
   runtime-owned backend slot), NOT through an implicit IS-A upcast.
   This is the supported migration path; the implicit IS-A path is
   gone.

3. **The const-pointer overload is exposed for read-only callers.** Tests
   and instrumentation routinely inspect counters / settings / counters
   without republishing the back-pointer to mutable state.

4. **Migration is mechanical for current callers.** The caller-side
   patch is one token: `engine.renderer().X(...)` →
   `(engine.renderer())->X(...)` (or `engine.renderer()->X(...)` when the
   accessor is called inline).  Callers that already used
   `engine.renderer().backend().X(...)` are *unaffected* (the upcast was
   already explicit there).

5. **No deprecation path.** The boundary refactor is itself the breaking
   change; per the canonical TICKET-011 cadence, a single coordinated
   rename wave ships the breaking change rather than maintaining a
   temporary `renderer_ref()` shim.  This keeps the docs surface single-
   source-of-truth (`Chronon3D::SDK` is the only documented public
   target per `docs/adr/INDEX.md`).

## Consequences

### Positive

* **The boundary stays clean.**  Single-identity inheritance eliminates
  the "is it BOTH a renderer and a backend?" doubt at every call site
  forever, and runs the boundary gate CI check automatically.

* **Callers cannot accidentally write to the polymorphic backend through
  the renderer reference.**  Today the call site must explicitly
  request the backend via `.backend()`, which is auditable in code
  review.

* **Return-by-pointer coexists naturally with lifetime annotation.**
  A future nullable lifetime (`weak_ptr<SoftwareRenderer>` or
  `observer_ptr<SoftwareRenderer>`) can ride on the same accessor.

### Negative

* **API break for any caller that wrote `engine.renderer().X(...)`.**
  The compile error is a dereference of a pointer (`engine.renderer()
  -> counters()`); easy to spot, but a churn point for downstream code.

* **Drop of the implicit upcast.**  Any caller that took
  `graph::RenderBackend&` and was secretly given a `SoftwareRenderer`
  through that reference now compiles fails with the well-known error
  `cannot initialize reference of type 'RenderBackend&' from expression
  of type 'SoftwareRenderer'`.  This is the same error class the
  in-tree `tests/render_graph/{builder,pipeline}/test_graph_*` tests
  tripped over (and were mechanically rewired in the test-graph
  follow-up on `codex/agent1-renderer-boundary`).

* **`noexcept` is a hard contract** — caller code that previously
  trapped a `throw` from a misformed renderer can now crash the
  process on null-deref.  Mitigated by the construction-order invariant
  (engine ctor → renderer ctor → accessor available) and by the
  pointer semantics being uniform with `m_runtime->X()` forwarders
  already in the same file.  Custom fixtures and partial-init tests
  SHOULD add a `assert(engine.renderer() != nullptr)` in CI / debug
  builds.

* **Source-compatibility AND binary-compatibility are BOTH broken for `engine.renderer()` specifically.**  The return-type change (`SoftwareRenderer&` → `SoftwareRenderer*`) alters the function's ABI signature at the engine boundary.  The shipped `.so` (and the static archive's embedded call sites) must therefore be rebuilt as part of the upgrade.  For **static-link consumers**, source-compat IS the ABI in practice — every consumer that statically links Chronon3D MUST be recompiled against the new SDK archive.  For **shared-link consumers**, the installed `.so` is rebuilt by the new branch and every consumer that called `engine.renderer()` MUST be recompiled against the new headers to pick up the new pointer-return signature.  The ABI surface for **other** Chronon3D entry points (those whose signature did NOT change in this refactor) is preserved — only `engine.renderer()` plus the matching `RenderPipeline` constructor are affected.
### Neutral

* **The pointer is not exposed via `Chronon3D::SDK::Handle` or any
  opaque-handle scheme.**  This is a deliberate scope cut; the SDK
  facade still exposes the concrete `SoftwareRenderer` so the API
  surface remains greppable.

* **Performance: pointer-deref on every accessor call.**  Each
  `engine.renderer()->X` now goes through one pointer dereference
  where the previous `engine.renderer().X` came from a stack
  reference.  Per-call CPU overhead is sub-nanosecond, dwarfed by the
  surrounding render work; macrobenchmark parity expected.  Hot
  callers (`counters()`, `motion_blur()`, `composition_registry()`,
  `video_decoder()`) read these pointer-deref'd values inside render
  loops but no profile regression is anticipated because the values
  are already cache-line-resident on the per-instance cache.

## Migration path (for the — currently zero third-party — external consumers)

> **Consumer scope.**  No third-party external consumers ship against
> Chronon3D today.  One **in-tree** SDK smoke consumer lives at
> `tests/package_consumer/CMakeLists.txt` + `tests/package_consumer/main.cpp`
> and was updated in lockstep with the boundary refactor.  The steps
> below apply to both the in-tree consumer and any future third-party
> consumer; both static- and shared-link consumers must recompile.

| Step | Action | Verification |
|---|---|---|
| 1 | Recompile consumer code. | `g++ -std=c++20 ...` should fail only with the dereference error on `engine.renderer()`. |
| 2 | At every call site: replace `engine.renderer().X` with `engine.renderer()->X` (or `(*engine.renderer()).X` if stylistically preferred). | Greppable: `grep -r 'engine\.renderer()\.' src/` should return zero hits. |
| 3 | Anywhere the consumer expected `RenderBackend&` to be implicitly constructed from the renderer, insert `.backend()` between the deref and the backend call: `engine.renderer()->backend().Y(...)`. | Greppable: `grep -r 'engine\.renderer()\.backend' src/` should return matches where appropriate. |
| 4 | Rebuild + run the consumer's test suite + lint with `clang-tidy --checks=clang-diagnostic-deprecated-declarations` to catch any stray references. | Test pass; lint clean. |
| 5 | (Optional, debug builds) add `assert(engine.renderer() != nullptr);` after each engine construction so partial-init paths crash loudly rather than null-deref silently. | CI / debug-build exit-non-zero on misuse. |
| 6 | Consumers that called **neither** `engine.renderer()` **nor** the `RenderPipeline` ctor remain ABI-stable (both signatures changed in lockstep, so any consumer that took a renderer-typed datum MUST recompile). | `nm -D libChronon3D.so | grep -E 'engine.*renderer\|RenderPipeline'` shows the new pointer-typed signatures for both; consumers with zero renderer-typed symbols in their TUs are unaffected. |

External consumers that wrote `engine.renderer()` and *only* accessed
per-instance fields (`counters()`, `motion_blur()`, `composition_registry()`,
`video_decoder()`, `set_settings(...)`, etc.) MUST ensure their accessor was
not through an implicit upcast — the previous boundary refactor was the
canonical moment to break this surface.

## References

* `tools/check_software_renderer_boundary.sh` — invariant I1 ("single
  identity").
* `include/chronon3d/api/render_engine.hpp` — `renderer()` accessor +
  matching `SoftwareRenderer*` return.
* `include/chronon3d/runtime/render_pipeline.hpp` — `RenderPipeline`
  constructor taking `SoftwareRenderer*`.
* `src/runtime/render_engine.cpp` — construction sequence that
  guarantees `m_renderer` is non-null when the accessor is called.
* `docs/stabilization-plan/04-cmake-module-registry.md` — public SDK
  facade (`Chronon3D::SDK` is the only documented consumer target).
* Branch `codex/agent1-renderer-boundary` — the implementation series.
  Refer to the branch HEAD for the latest commit series (do not cite
  individual commit hashes from the ADR; commits are rebased / squashed
  routinely during the boundary refactor PR cycle).
