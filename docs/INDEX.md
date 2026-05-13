# Documentation Index

## Technical Notes

- [Architecture](../ARCHITECTURE.md)
- [Roadmap](../ROADMAP.md)
- [3D Subsystem](3d_subsystem.md)

## Build And Workflow

- [Building](../BUILDING.md)
- [README](../README.md)

## Code Areas

- `include/chronon3d/` - public API and domain model
- `src/renderer/` - software renderer and image/text/frame utilities
- `src/scene/` - scene building and evaluation helpers
- `src/evaluation/` - composition and timeline evaluation
- `apps/chronon3d_cli/` - CLI entry point
- `examples/` - registered compositions and proofs
- `tests/` - doctest-based validation

## Current Direction

Chronon3d is currently focused on:

- keeping CMake and xmake aligned
- stabilizing CI and build scripts
- adding a render graph and cache layers
- improving text, video, and audio workflow support
