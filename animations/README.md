# Chronon3D Animations

This folder is the public-facing home for reusable animation content.

## Layout

- `include/chronon3d/api/animations.hpp`: stable API for listing animation clips and exporting the manifest.
- `src/animations/animation_catalog.cpp`: built-in catalog implementation.
- `tests/animations/`: validation tests for the animation catalog and future clip contracts.
- `animations/`: reserved for data-driven animation assets and manifests that other tools can consume.

## Intended use

The renderer stays focused on drawing. Animations live here as ready-made reusable clips, with a small API surface that is simple to consume from C++, CLI tooling, or other languages through the JSON manifest.
