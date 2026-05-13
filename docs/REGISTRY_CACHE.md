# Registry And Cache Foundation

Chronon3d now has a first-pass registry and cache layer that sits above the renderer.

## Registry Layer

Registries expose stable IDs and metadata for the main engine domains:

- `EffectRegistry`
- `SourceRegistry`
- `ShapeRegistry`
- `SamplerRegistry`

The current implementation is intentionally conservative:

- IDs are stable strings.
- Registries reject duplicates.
- Built-ins are registered centrally.
- The code is metadata-first and does not yet rewrite the renderer pipeline.

## Cache Layer

The cache foundation introduces stable keys for the two most important render scopes:

- `NodeCache`
- `FrameCache`

The cache keys are based on stable hashes for:

- scope or composition identity
- frame number
- dimensions
- parameter hashes
- source hashes
- input hashes

This is the groundwork for precomposition, video input, and reusable render nodes.

## Direction

The next phase should wire these registries and caches into:

- precomposition evaluation
- video decoding
- text layout caching
- node-level render reuse
