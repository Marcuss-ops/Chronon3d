# Work Package 7 — Complete the software backend

## Goal

Make `SoftwareBackend` the only software rendering backend and reduce `SoftwareRenderer` to orchestration or remove it from backend responsibilities.

## TODO

### PR 7.0
- [ ] Inventory every method implemented by `SoftwareRenderer` because processors require it.
- [ ] Map each method to backend operation, orchestration, settings, telemetry, or resource service.

### PR 7.1
- [ ] Introduce a narrow `ShapeDrawContext`.
- [ ] Change shape processors to depend on that context instead of `SoftwareRenderer&`.
- [ ] Include only image, asset, text, settings, and counter services actually needed.

### PR 7.2
- [ ] Introduce one `TextRasterService`.
- [ ] Consolidate font and text-run caches.
- [ ] Route text drawing through the service.
- [ ] Eliminate duplicate static font-resource caches.

### PR 7.3
- [ ] Implement every advertised operation directly in `SoftwareBackend`.
- [ ] Cover node drawing, text runs, blur, effects, compositing, and per-pixel depth of field.
- [ ] Ensure capabilities match real implementations.

### PR 7.4
- [ ] Introduce or complete `SoftwareBackendServices`.
- [ ] Provide settings, counters, framebuffer pool, shape registry, text service, and asset resolver explicitly.
- [ ] Avoid backend access through `SoftwareRenderer`.

### PR 7.5
- [ ] Stop `SoftwareRenderer` from inheriting `RenderBackend`.
- [ ] Route pipeline rendering through `RenderRuntime::backend()`.
- [ ] Remove backend-to-renderer casts from graph pipeline code.

### PR 7.6
- [ ] Review whether `SoftwareRenderer` still adds value.
- [ ] Keep it only as a thin per-instance orchestrator if required.
- [ ] Move remaining engine-lifetime ownership to `RenderRuntime`.

### PR 7.7
- [ ] Test backend operations without constructing `SoftwareRenderer`.
- [ ] Test capability and implementation consistency.
- [ ] Test text-enabled and text-disabled builds.
- [ ] Test one isolated software render through `RenderBackend`.

### PR 7.8
- [ ] Add checks preventing graph pipeline dependencies on `software_renderer.hpp`.
- [ ] Add checks preventing processor signatures from taking `SoftwareRenderer&`.
- [ ] Add checks preventing `SoftwareRenderer` from inheriting `RenderBackend`.

## Exit criteria

- [ ] `SoftwareBackend` implements every capability it advertises.
- [ ] Processors use narrow contexts and services.
- [ ] Graph pipeline contains no renderer cast.
- [ ] `SoftwareRenderer` is no longer a backend.
