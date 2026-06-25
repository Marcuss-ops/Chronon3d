# Work Package 7 — Remaining software backend work

## Current state

`SoftwareBackend` exists, but `SoftwareRenderer` still inherits and implements `RenderBackend`. The graph pipeline still performs `dynamic_cast<SoftwareRenderer*>`, and processors still depend on renderer-owned services rather than narrow backend contracts.

## Goal

Make `SoftwareBackend` the only software rendering backend and reduce `SoftwareRenderer` to orchestration.

## TODO

### PR 7.0 — Inventory current renderer responsibilities

- [ ] Classify every `SoftwareRenderer` method as backend operation, orchestration, settings, telemetry, cache access, or resource service.
- [ ] List every processor API that takes `SoftwareRenderer&`.
- [ ] List every graph-pipeline cast or include that depends on the software renderer.
- [ ] Identify which counters/settings currently force backend construction to depend on renderer lifetime.

### PR 7.1 — Introduce narrow backend services

- [ ] Add `SoftwareBackendServices` with explicit settings, counters, framebuffer pool, registries, image service, text service, and asset resolver.
- [ ] Add a narrow shape draw context for processor execution.
- [ ] Replace processor dependencies on `SoftwareRenderer&` with typed services.
- [ ] Avoid another generic all-purpose service locator.

### PR 7.2 — Consolidate text rasterization

- [ ] Introduce one `TextRasterService`.
- [ ] Consolidate font-face, text-run, glow, and shadow cache ownership.
- [ ] Route backend text operations through the service.
- [ ] Remove duplicate static text/font caches.
- [ ] Test text-enabled and text-disabled builds.

### PR 7.3 — Complete `SoftwareBackend`

Implement and test all advertised operations directly:

- [ ] node drawing
- [ ] text runs
- [ ] blur
- [ ] effect stacks
- [ ] layer compositing
- [ ] per-pixel depth of field
- [ ] framebuffer-pool access
- [ ] render counters and capability reporting

- [ ] Make capability flags match actual implementations.
- [ ] Test backend operations without constructing `SoftwareRenderer`.

### PR 7.4 — Remove renderer/backend dual identity

- [ ] Stop `SoftwareRenderer` from inheriting `RenderBackend`.
- [ ] Attach a real `SoftwareBackend` to `RenderRuntime` without backend-to-renderer borrowing.
- [ ] Route graph rendering through `RenderRuntime::backend()`.
- [ ] Remove `dynamic_cast<SoftwareRenderer*>` from pipeline code.
- [ ] Pass debug configuration, catalogs, scheduler, session, and resolver through typed inputs.
- [ ] Keep `SoftwareRenderer` only as a thin job/orchestration facade if still useful.

### PR 7.5 — Add backend boundary tests

- [ ] Graph pipeline does not include `software_renderer.hpp`.
- [ ] Processor signatures do not take `SoftwareRenderer&`.
- [ ] `SoftwareRenderer` does not inherit `RenderBackend`.
- [ ] Every advertised backend capability has a direct test.
- [ ] A non-software backend can enter graph orchestration without a software cast.
- [ ] Backend lifetime is owned by `RenderRuntime` and independent from renderer internals.

## Dependencies

Complete Work Packages 5 and 6 first so backend work does not mask precomp/session lifetime bugs.

## Exit criteria

- [ ] `SoftwareBackend` implements every capability it advertises.
- [ ] Processors use narrow typed services.
- [ ] Pipeline contains no backend-to-renderer cast.
- [ ] `SoftwareRenderer` is no longer a backend.
- [ ] Backend behavior is testable without renderer construction.
