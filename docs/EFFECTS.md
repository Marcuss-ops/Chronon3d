# Effects System

Chronon3d treats effects as named, registrable units with stable IDs. The goal is to keep new effects discoverable and composable without growing `SoftwareRenderer` into a monolith.

## Rules

- Every effect has a stable ID.
- Every effect has a category.
- Every effect has a stage.
- Every concrete effect owns a typed params struct.
- New effects are registered through a registry.
- `SoftwareRenderer` does not gain ad-hoc effect implementations.
- Builder shortcuts are allowed only as convenience wrappers.

## Categories

- `blur`
- `color`
- `light`
- `stylize`
- `distort`
- `geometry`
- `temporal`
- `composite`

## Stages

- `node`
- `layer_pre_transform`
- `layer_post_transform`
- `adjustment`
- `composition`
- `temporal`

## Current Built-ins

Chronon3d currently announces the following built-ins in `EffectRegistry`:

- `blur.gaussian`
- `color.tint`
- `color.brightness`
- `color.contrast`
- `light.drop_shadow`
- `light.glow`

These are the registry-level definitions only. They establish taxonomy and metadata before deeper renderer integration.

## Direction

The next steps after this foundation are:

- route `LayerBuilder` convenience methods through effect instances
- resolve layer effects via the registry
- add more effects only after the registry and descriptors stay stable
