# Chronon3d Architecture

Chronon3d is a code-first, headless, CPU-only motion graphics engine. The goal is not to emulate a full 3D DCC tool; the goal is to provide a maintainable rendering engine for scripted compositions, proof scenes, automated video generation, and repeatable frame rendering.

## Core Principles

- Code-first composition is the primary authoring model.
- Rendering must remain deterministic for the same inputs.
- The engine stays headless; UI tooling lives outside the render core.
- CPU execution is the baseline target.
- External tools such as `ffmpeg` are allowed at the workflow boundary, but not embedded into the render core unless there is a clear reason.

## Current Topology

- `include/chronon3d/` exposes the public API and domain model.
- `src/renderer/` contains the software renderer and frame-level rendering utilities.
- `src/scene/` turns scene builders into renderable scene structures.
- `src/evaluation/` bridges timeline/composition inputs into evaluated runtime state.
- `src/io/` handles image output.
- `apps/chronon3d_cli/` provides the command-line interface.
- `templates/` contains reusable scene templates and composition helpers.
- `tests/` validates the engine behavior.

## Runtime Flow

1. A `Composition` is evaluated for a frame.
2. The composition builds a scene using `SceneBuilder` and related builders.
3. `GraphBuilder` converts the scene into a `RenderGraph` (DAG of render nodes).
4. `GraphExecutor` traverses the graph, caching node outputs by content hash via `NodeCache`.
5. The compositor produces a final `Framebuffer`.
6. The CLI optionally turns frame sequences into video with `ffmpeg`.

## Render Graph

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
