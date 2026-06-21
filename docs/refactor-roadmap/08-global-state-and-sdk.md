# Work Package 8 — Remaining global-state and SDK work

## Current state

Completed:

- `RenderRuntime` owns a typed engine-local `AssetResolver`.
- Resolver behavior for absolute, relative, missing, mounted, and escaping paths is implemented and tested.
- Deep font/text/preflight consumers no longer call the removed one-argument string resolver.
- The legacy `AssetRegistry::resolve_path()` shortcut is removed.

The remaining problem is that deep consumers reach the typed resolver through process-global selection. `g_active_runtime`, `g_process_wide_assets_root`, `typed_resolver_for_deep_code()`, and the resolver mirror still allow one runtime to influence another. The public SDK facade also exposes software implementation types.

## TODO

### PR 8.0 — Pass the resolver explicitly

- [ ] Add `AssetResolver*` to the narrow render/session/context services that genuinely need path resolution.
- [ ] Pass the runtime-owned resolver into font loading, text rasterization, image loading, preflight, precomp construction, content helpers, CLI commands, and tests.
- [ ] Remove deep calls to `typed_resolver_for_deep_code()`.
- [ ] Do not replace the bridge with another singleton or thread-local active resolver.
- [ ] Keep the two-argument explicit-root helper only where the caller intentionally owns the root.

### PR 8.1 — Remove process-wide runtime and asset state

Remove:

- [ ] `g_active_runtime`
- [ ] `set_active_runtime()`
- [ ] `active_runtime()`
- [ ] `g_process_wide_assets_root`
- [ ] `set_process_wide_assets_root()`
- [ ] `process_wide_assets_root()`
- [ ] `default_assets_root_for_deep_code()`
- [ ] `typed_resolver_for_deep_code()`
- [ ] `g_deep_resolver_mirror`
- [ ] test-only global resolver reset hooks

Also:

- [ ] Stop `RenderRuntime::populate()` from publishing itself globally.
- [ ] Stop `RenderRuntime::set_default_assets_root()` from writing process state.
- [ ] Keep runtime destruction free from global-pointer cleanup.

### PR 8.2 — Add multi-engine isolation tests

- [ ] Construct two runtimes with different asset roots.
- [ ] Resolve the same relative path independently.
- [ ] Run font/text/preflight resolution concurrently for both runtimes.
- [ ] Destroy one runtime and verify the other remains valid.
- [ ] Verify CLI and tests work through explicit resolver wiring.
- [ ] Verify no last-created/last-mounted runtime changes another engine's result.

### PR 8.3 — Close the standard SDK facade

File:
- `include/chronon3d/api/render_engine.hpp`

Actions:

- [ ] Remove direct include of `software_renderer.hpp`.
- [ ] Remove direct exposure of software-session implementation types.
- [ ] Remove or relocate `renderer()`, `runtime()`, and `create_session()` from the standard facade.
- [ ] Move necessary expert access to an explicitly advanced/internal header.
- [ ] Keep ordinary render, settings, assets, and cache operations behind PIMPL.
- [ ] Update comments that still mention retired `ExecutionPlanCache` or process-wide asset fallback.

### PR 8.4 — Remove compatibility and contract leaks

- [ ] Remove `using RenderFrameInfo = FrameInput` after call-site migration.
- [ ] Replace `default_assets_root` string pointers in session services with explicit resolver access.
- [ ] Remove stale public comments describing Phase A global mirroring.
- [ ] Ensure public headers do not require backend implementation headers transitively.

### PR 8.5 — Add SDK and global-state guards

- [ ] Compile a consumer translation unit that includes only `api/render_engine.hpp`.
- [ ] Confirm the header does not include software renderer/session implementation headers.
- [ ] Build and run the standalone install consumer through `Chronon3D::SDK` only.
- [ ] Fail architecture checks if active-runtime or process-wide asset globals return.
- [ ] Fail architecture checks if the standard facade returns software implementation types.
- [ ] Verify advanced/internal headers are unnecessary for ordinary rendering.

## Dependencies

- Session resolver wiring should align with Work Package 3.
- Public facade cleanup should follow Work Package 7 backend separation.

## Exit criteria

- [ ] Asset resolution is explicit and engine-local end to end.
- [ ] No active-runtime, process-wide-root, or global typed-resolver bridge remains.
- [ ] Two runtimes cannot contaminate each other.
- [ ] Standard SDK headers expose no software implementation types.
- [ ] Ordinary consumers build and run through `Chronon3D::SDK` only.
