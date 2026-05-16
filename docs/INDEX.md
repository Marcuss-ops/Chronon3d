# Documentation Index

## Technical Notes

- [Architecture](../ARCHITECTURE.md)
- [Roadmap](../ROADMAP.md)
- [Render Graph](RENDER_GRAPH.md)
- [Proofs Policy](PROOFS_POLICY.md)
- [Effects](EFFECTS.md)
- [Registry And Cache](REGISTRY_CACHE.md)
- [3D Subsystem](3d_subsystem.md)
- [Project Model](PROJECT_MODEL.md)
- [Unified Orchestration](UNIFIED_ORCHESTRATION.md)
- [Development Notes](DEVELOPMENT_NOTES.md)

## Build And Workflow

- [Building](../BUILDING.md) - start here for a new machine
- [Validation Checklist](../CHECKLIST.md) - pre-merge validation protocol
- [README](../README.md)

## Code Areas

- `include/chronon3d/` - public API and domain model
- `include/chronon3d/render_graph/` - new modular render graph (nodes, executor, builder)
- `src/renderer/` - software renderer and image/text/frame utilities
- `src/render_graph/` - render graph implementation (executor, builder, profiler)
- `src/scene/` - scene building and evaluation helpers
- `src/evaluation/` - composition and timeline evaluation
- `apps/chronon3d_cli/` - CLI entry point
- `tests/` - doctest-based validation

## Current Direction

- CMake + vcpkg is the single build path
- Render graph architecture is the production path: nodes, LRU cache, profiler, builder, executor
- Effect system uses `EffectStack` throughout - `LayerEffect` legacy removed from public API
- `use_modular_graph` now controls coordinate mode compatibility only
- Next: keep trimming builder responsibilities, expand shared utilities across clips, and add focused regression tests for graph features
