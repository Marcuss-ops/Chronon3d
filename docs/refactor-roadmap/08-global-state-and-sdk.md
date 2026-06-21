# Work Package 8 — Remove global state and close the SDK surface

## Goal

Eliminate process-wide runtime and asset lookup, then keep software internals out of the main public facade.

## TODO

### PR 8.0
- [ ] Inventory every use of the active runtime pointer and process-wide asset root.
- [ ] Classify each caller as font, text, image, preflight, content, CLI, test, or other.

### PR 8.1
- [ ] Introduce one typed `AssetResolver` owned by `RenderRuntime`.
- [ ] Define relative, absolute, missing-root, and mounted-root behavior.
- [ ] Keep path resolution deterministic and engine-local.

### PR 8.2
- [ ] Add the resolver to the appropriate runtime/render services.
- [ ] Pass it to font, text, image, preflight, and precomp code.
- [ ] Avoid hidden lookups from deep code.

### PR 8.3
- [ ] Migrate CLI and tests to construct or receive an explicit resolver/runtime.
- [ ] Avoid process-level setup helpers for ordinary rendering tests.

### PR 8.4
- [ ] Retire the active runtime pointer.
- [ ] Retire the process-wide asset-root fallback.
- [ ] Retire deep-code helper functions that read either global.

### PR 8.5
- [ ] Reduce `api/render_engine.hpp` to the supported SDK facade.
- [ ] Keep PIMPL-only internals out of public includes.
- [ ] Move direct renderer, runtime, and software-session access to an advanced/internal header if still required.

### PR 8.6
- [ ] Ensure the public header does not include software renderer or software session headers.
- [ ] Keep `Chronon3D::SDK` as the documented consumer target.
- [ ] Run the standalone install-consumer test.

### PR 8.7
- [ ] Add a two-engine isolation test.
- [ ] Give each engine a different asset root.
- [ ] Render concurrently and verify no cross-engine path resolution.

### PR 8.8
- [ ] Add boundary checks preventing global runtime and asset-root helpers from returning.
- [ ] Add a public-header compile test using only `api/render_engine.hpp`.

## Exit criteria

- [ ] Asset lookup is explicit and engine-local.
- [ ] Two engines cannot contaminate each other.
- [ ] Main SDK headers expose no software implementation types.
- [ ] Installed consumer links and runs through `Chronon3D::SDK` only.
