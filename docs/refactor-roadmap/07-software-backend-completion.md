# Work Package 7 — Remaining software backend work## Current state

`SoftwareBackend` exists, but `SoftwareRenderer` still inherits `RenderBackend` and the graph pipeline still uses `dynamic_cast<SoftwareRenderer*>`. Narrow typed processor services (`ShapeDrawContext` or equivalent) and a consolidated `TextRasterService` do not yet exist.

## Goal

Make `SoftwareBackend` the only software rendering backend and reduce `SoftwareRenderer` to orchestration.

## TODO

### PR 7.0 — Inventory renderer responsibilities

- [ ] Classify every `SoftwareRenderer` method as backend operation, orchestration, settings, telemetry, cache access, or resource service.
- [ ] Identify processor APIs that still require `SoftwareRenderer&`.
- [ ] Identify graph-pipeline casts to `SoftwareRenderer`.

### PR 7.1 — Introduce narrow draw services

- [ ] Add a narrow `ShapeDrawContext` or equivalent typed service bundle.
- [ ] Change shape processors to depend on explicit services rather than `SoftwareRenderer&`.
- [ ] Expose only image, asset, text, settings, counters, and pools actually needed.
- [ ] Avoid creating another generic service locator.

### PR 7.2 — Consolidate text rasterization

- [ ] Introduce one `TextRasterService`.
- [ ] Consolidate font-face, text-run, glow, and shadow cache ownership.
- [ ] Route text drawing through the service.
- [ ] Remove duplicate static text/font caches.
- [ ] Test text-enabled and text-disabled builds.

### PR 7.3 — Complete `SoftwareBackend`

Implement and test all advertised backend operations directly:

- [ ] node drawing
- [ ] text runs
- [ ] blur
- [ ] effect stacks
- [ ] layer compositing
- [ ] per-pixel depth of field
- [ ] framebuffer-pool and counter access through typed backend services

- [ ] Make capabilities match actual implementations.
- [ ] Test backend operations without constructing `SoftwareRenderer`.

### PR 7.4 — Remove renderer/backend dual identity

- [ ] Stop `SoftwareRenderer` from inheriting `RenderBackend`.
- [ ] Route graph rendering through `RenderRuntime::backend()`.
- [ ] Remove `dynamic_cast<SoftwareRenderer*>` from graph pipeline code.
- [ ] Pass debug configuration, catalogs, scheduler, and session through typed inputs.
- [ ] Keep `SoftwareRenderer` only as a thin per-instance orchestrator if still useful.

### PR 7.5 — Add boundary tests

- [ ] Graph pipeline must not include `software_renderer.hpp`.
- [ ] Processor signatures must not take `SoftwareRenderer&`.
- [ ] `SoftwareRenderer` must not inherit `RenderBackend`.
- [ ] Backend capability tests must exercise each advertised operation.
- [ ] A non-software backend must be able to enter graph orchestration without a software cast.

## Exit criteria

- [ ] `SoftwareBackend` implements every capability it advertises.
- [ ] Processors use narrow typed services.
- [ ] Graph pipeline contains no backend-to-renderer cast.
- [ ] `SoftwareRenderer` is no longer a backend.
- [ ] Backend behavior is testable without renderer construction.
