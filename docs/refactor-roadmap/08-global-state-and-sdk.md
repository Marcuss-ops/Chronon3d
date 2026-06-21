# Work Package 8 — Remaining global-state and SDK work

## Goal

Remove process-wide runtime and asset state, then keep software implementation types out of the main SDK facade.

Resolved render-session include leaks and the retired execution-plan cache are no longer tracked here.

## TODO

### PR 8.0 — Introduce an engine-local asset resolver

- [ ] Add one typed `AssetResolver` owned by `RenderRuntime`.
- [ ] Define absolute, relative, mounted-root, and missing-asset behavior.
- [ ] Make resolution deterministic and independent for each engine.
- [ ] Expose the resolver through narrow render services.

### PR 8.1 — Migrate deep asset consumers

Migrate explicit resolver access into:

- [ ] font loading
- [ ] text rasterization
- [ ] image loading
- [ ] preflight
- [ ] precomp construction
- [ ] content helpers
- [ ] CLI render commands
- [ ] tests and fixtures

- [ ] Do not replace one global with another service-locator singleton.

### PR 8.2 — Remove process-wide runtime state

- [ ] Remove `g_active_runtime`.
- [ ] Remove `set_active_runtime()` and `active_runtime()`.
- [ ] Remove `g_process_wide_assets_root`.
- [ ] Remove process-wide asset-root setters/getters.
- [ ] Remove `default_assets_root_for_deep_code()`.
- [ ] Stop `RenderRuntime::set_default_assets_root()` from publishing global state.

### PR 8.3 — Add multi-engine isolation tests

- [ ] Construct two runtimes with different asset roots.
- [ ] Resolve the same relative path independently.
- [ ] Render concurrently and verify no cross-engine contamination.
- [ ] Destroy one runtime and verify the other remains valid.
- [ ] Verify tests and CLI can operate without an active-runtime fallback.

### PR 8.4 — Close the public SDK facade

File:
- `include/chronon3d/api/render_engine.hpp`

Actions:
- [ ] Remove direct include of `software_renderer.hpp`.
- [ ] Remove direct include of software session implementation headers.
- [ ] Keep implementation details behind `RenderEngine::Impl`.
- [ ] Remove `renderer()`, `runtime()`, and software-session access from the standard facade.
- [ ] Move necessary expert access to an explicitly advanced/internal header.
- [ ] Keep `Chronon3D::SDK` as the documented consumer target.

### PR 8.5 — Remove remaining compatibility aliases

- [ ] Remove `using RenderFrameInfo = FrameInput` after call-site migration.
- [ ] Remove public comments and docs that still describe the retired plan cache.
- [ ] Remove public API text that describes process-wide asset fallback as supported behavior.

### PR 8.6 — Add SDK boundary tests

- [ ] Compile a translation unit that includes only `api/render_engine.hpp`.
- [ ] Confirm that header does not pull software renderer/session headers transitively.
- [ ] Build and run the standalone install consumer.
- [ ] Verify consumers cannot link internal targets through the documented namespace.
- [ ] Verify advanced/internal headers are not required for ordinary rendering.

### PR 8.7 — Add permanent guards

- [ ] Prevent active-runtime globals from returning.
- [ ] Prevent process-wide asset-root globals from returning.
- [ ] Prevent `api/render_engine.hpp` from including software implementation headers.
- [ ] Prevent standard SDK facade methods from returning software implementation types.

## Separate follow-up

The optional `FrameGraphCompiler` structural-reuse optimization is tracked as `TICKET-008` in `docs/FOLLOWUP_TICKETS.md`. It is not part of global-state removal.

## Exit criteria

- [ ] Asset lookup is explicit and engine-local.
- [ ] Two runtimes cannot contaminate each other.
- [ ] No active-runtime or process-wide asset-root state remains.
- [ ] Main SDK headers expose no software implementation types.
- [ ] Ordinary consumers build and run through `Chronon3D::SDK` only.
