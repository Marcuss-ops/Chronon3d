# Chronon3d Architecture

Chronon3d is a code-first, headless, CPU-only motion graphics engine. The goal is to provide a maintainable rendering engine for scripted compositions, proof scenes, automated video generation, and repeatable frame rendering.

## Core Principles

- **Code-first**: Composition is the primary authoring model.
- **Deterministic**: Same inputs, same pixels.
- **Headless**: No UI in the core.
- **CPU-only**: Focus on maintainable, SIMD-optimized software rendering.

For detailed guidelines on contributing and the project mantra, see [CONTRIBUTING.md](docs/CONTRIBUTING.md).

> **core only builds, adapters only call, data only flows in.**
>
> *Core defines the domain. Runtime orchestrates rendering.*
> *Backends rasterize, decode, encode, and perform concrete IO.*
> *Operations expose thin adapters. CLI parses human intent only.*
> *Tools and examples must never become architecture.*

## Topology

- `include/chronon3d/` — Public API (see [api/](include/chronon3d/api/)).
- `src/runtime/` — Pipeline and evaluation.
- `src/render_graph/` — Execution model.
- `src/backends/software/` — Main software rasterizer.
- `apps/chronon3d_cli/` — Human entry point.

## Runtime Flow

1. **Evaluation**: `Composition` resolves time to `Scene`.
2. **Graph Building**: `Scene` converts to `RenderGraph`.
3. **Execution**: `GraphExecutor` processes nodes, hitting `NodeCache`.
4. **Compositing**: `SoftwareCompositor` blends layers into a `Framebuffer`.

## More Documentation

- [Testing Philosophy](docs/TESTING.md)
- [Roadmap & Vision](docs/ROADMAP.md)
- [Building Guide](BUILDING.md)

The render graph (`chronon3d::graph`) is the execution model for a single frame. Each operation — source rendering, masking, effects, compositing, precomps — is a `RenderGraphNode` with explicit inputs and outputs. The executor caches node results by content hash so unchanged subtrees are never re-executed.

See [docs/RENDER_GRAPH.md](docs/RENDER_GRAPH.md) for the full specification.

## Build Model

CMake + vcpkg is the primary build path. All dependencies are managed via `vcpkg.json` and installed automatically on first configure.

```bash
export VCPKG_ROOT=~/vcpkg
cmake --preset linux-release
cmake --build build/chronon/linux-release -j$(nproc)
```

See [BUILDING.md](BUILDING.md) for the complete guide.

## Internal Layers

### Public API

The public headers define the surface area for:

- animation
- math and color
- scene and layer building
- rendering primitives
- composition registration
- asset and cache abstractions

### Scene Model

The scene layer is intentionally descriptive. It captures what should be drawn, in what order, with which transforms, masks, and effects.

### Evaluation

The evaluation layer resolves code-first composition logic into frame-specific runtime data. This is where animation values, timelines, sequences, and frame context are applied.

### Rendering

The renderer is software-based and currently handles:

- shapes
- images
- text
- masks
- layer transforms
- blend modes
- 2.5D camera projection
- basic layer effects

The next structural step is a render graph with explicit passes and cacheable node outputs.

## Data Flow Boundaries

Chronon3d should keep these responsibilities separate:

- Composition code decides intent.
- Evaluation resolves time-dependent values.
- Scene objects describe the frame.
- Rendering performs pixel work.
- IO writes the final frame or frame sequence.

This separation keeps the engine maintainable as features grow.

## Testing Philosophy

The `tests/` directory must remain as minimal as possible.

Tests exist to verify the behavior of the engine, not to recreate engine logic, duplicate runtime flows, or become an alternative implementation of the system.

Every extra line written inside a test is a signal that something may be missing, misplaced, or poorly exposed in the real codebase. If a test needs too much setup, too much scene-building logic, or too much repeated boilerplate, the first assumption should be that the production API, a helper, a fixture, or a dedicated utility is missing.

A long test is usually a sign of weak boundaries.

The responsibility of the test is only to express:

1. the scenario being tested
2. the action being performed
3. the expected result

The test should not contain reusable engine logic.

If logic is useful for the runtime, CLI, examples, demos, or real rendering workflows, it must live in the proper production area, not in `tests/`.

If logic is useful only to reduce test repetition, it should live in a test helper or fixture file, not be repeated across many test cases.

Preferred structure:

```txt
tests/
  helpers/
    render_test_utils.hpp
    pixel_assertions.hpp
    scene_fixtures.hpp
    render_graph_fixtures.hpp

  core/
  scene/
  render_graph/
  renderer/
  effects/
  cache/
  io/
```

A test should be short and intention-revealing.

Good:

```cpp
TEST_CASE("near 2.5D layer covers far layer") {
    auto fb = test::render_modular_frame(
        test::scenes::near_red_far_blue_25d()
    );

    test::expect_red(*fb, 100, 100);
}
```

Bad:

```cpp
TEST_CASE("near 2.5D layer covers far layer") {
    Composition comp = composition(...);
    SceneBuilder s(...);
    s.camera_2_5d(...);
    s.layer(...);
    s.layer(...);
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);
    auto fb = renderer.render_frame(comp, 0);
    CHECK(...);
}
```

The second example repeats setup that should either belong to the production API or to a test helper.

### Rules

* Do not put runtime features inside `tests/`.
* Do not register production compositions from test files.
* Do not duplicate scene-building logic across tests.
* Do not make tests responsible for knowing too much about renderer setup.
* Do not use tests as a place to hide missing engine APIs.
* Prefer helpers, fixtures, presets, or production utilities over long test bodies.
* The longer a test becomes, the more suspicious it should be considered.
* Tests should verify behavior, not compensate for missing architecture.

### Guiding principle

If a test becomes long because it needs to rebuild too much of the system manually, the solution is not to accept the long test.

The solution is to move the repeated responsibility to the correct place:

```txt
Production behavior       -> src/ or include/
Reusable demo composition -> src/compositions/ or examples/
Test-only setup           -> tests/helpers/
Assertions                -> inside the test
```

The goal is simple:

```txt
Tests should be the final check, not a second implementation.
```

## Test Minimalism Rule

Tests must be minimal.

Every unnecessary line inside a test is a sign that the real code may be missing an API, helper, fixture, preset, or properly placed responsibility.

A test should not rebuild the engine from scratch. It should only describe the scenario, execute the behavior, and verify the result.

If the same setup appears in multiple tests, extract it.

If the setup represents real engine behavior, move it into production code.

If the setup exists only for testing, move it into `tests/helpers`.

Long tests are not a badge of completeness. Long tests are usually a symptom of poor boundaries.

The shorter and clearer the test, the healthier the architecture.

## Near-Term Engine Direction

The next architectural additions should be:

- render graph and render passes
- node and asset registries
- cache layers for geometry, text layout, and render results
- precomposition support
- video source handling
- audio timeline metadata
- more formal color and sampling pipelines

Those additions should happen without turning `SoftwareRenderer` into an ever-growing monolith.
