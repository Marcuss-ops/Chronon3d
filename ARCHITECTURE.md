# Chronon3d Architecture

Chronon3d is a code-first, headless, CPU-only motion graphics engine. The goal is to provide a maintainable rendering engine for scripted compositions, proof scenes, automated video generation, and repeatable frame rendering.

## Core Principles

- **Code-first**: Composition is the primary authoring model.
- **Deterministic**: Same inputs, same pixels.
- **Headless**: No UI in the core.
- **CPU-only**: Focus on maintainable, SIMD-optimized software rendering.

For detailed guidelines on contributing and the project mantra, see [CONTRIBUTING.md](docs/CONTRIBUTING.md).

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
