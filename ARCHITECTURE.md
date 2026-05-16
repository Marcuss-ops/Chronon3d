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

- `include/chronon3d/` — Public API.
- `src/runtime/` — Pipeline and evaluation.
- `src/render_graph/` — Execution model.
- `src/backends/software/` — Main software rasterizer.
- `apps/chronon3d_cli/` — Human entry point.

## Runtime Flow

1. **Evaluation**: `Composition` resolves time to `Scene`.
2. **Graph Building**: `Scene` converts to `RenderGraph`.
3. **Execution**: `GraphExecutor` processes nodes, hitting `NodeCache`.
4. **Compositing**: `SoftwareCompositor` blends layers into a `Framebuffer`.

## Internal Layers

### Public API
The public headers define the surface area for animation, math, scene building, and rendering primitives.

### Scene Model
Captures what should be drawn, in what order, with which transforms, masks, and effects.

### Evaluation
Resolves code-first composition logic into frame-specific runtime data (animations, timelines).

### Rendering
Software-based rasterization handling shapes, images, text, masks, 2.5D projection, and effects.

## Render Graph (modular)

The render graph (`chronon3d::graph`) is the execution model for a single frame. Each operation — source rendering, masking, effects, compositing — is a `RenderGraphNode` with explicit inputs and outputs.

### Key Characteristics:
- **Coordinate Systems**: Uses top-left (screen) coordinates for 2D layers and centered coordinates for 3D/Projected layers.
- **Dependency Caching**: `GraphExecutor` combines node hashes with input digests to ensure correct invalidation.
- **Adjustment Layers**: Apply effects to previous results without a source.

## Data Flow Boundaries

Chronon3d keeps these responsibilities separate:
- Composition code decides intent.
- Evaluation resolves time-dependent values.
- Scene objects describe the frame.
- Rendering performs pixel work.
- IO writes the final frame.

## Testing Philosophy

The `tests/` directory must remain as minimal as possible.

Tests exist to verify the behavior of the engine, not to recreate engine logic. A long test is usually a sign of weak boundaries.

Preferred structure:
```txt
tests/
  helpers/
    render_test_utils.hpp
    pixel_assertions.hpp
    scene_fixtures.hpp
  core/
  scene/
  render_graph/
  renderer/
```

## Near-Term Engine Direction
- node and asset registries
- precomposition support
- video source handling
- audio timeline metadata
