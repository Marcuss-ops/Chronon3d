# Documentation Index

## Technical Notes

- [Architecture](../ARCHITECTURE.md)
- [Roadmap](../ROADMAP.md)
- [Render Graph](RENDER_GRAPH.md)
- [Effects](EFFECTS.md)
- [Registry And Cache](REGISTRY_CACHE.md)
- [3D Subsystem](3d_subsystem.md)

## Build And Workflow

- [Building](../BUILDING.md) — start here for a new machine
- [README](../README.md)

## Code Areas

- `include/chronon3d/` — public API and domain model
- `include/chronon3d/render_graph/` — new modular render graph (nodes, executor, builder)
- `src/renderer/` — software renderer and image/text/frame utilities
- `src/render_graph/` — render graph implementation (executor, builder, profiler)
- `src/scene/` — scene building and evaluation helpers
- `src/evaluation/` — composition and timeline evaluation
- `apps/chronon3d_cli/` — CLI entry point
- `templates/` — reusable scene templates and composition helpers
- `tests/` — doctest-based validation

## Current Direction

- CMake + vcpkg is the single build path (xmake no longer maintained)
- Render graph architecture complete: nodes, LRU cache, profiler, builder, executor
- Effect system uses `EffectStack` throughout — `LayerEffect` legacy removed from public API
- Next: wire `GraphBuilder` into `SoftwareRenderer` production path, then `VideoSource`
