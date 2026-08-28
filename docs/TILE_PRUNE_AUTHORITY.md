# Tile-prune authority

## Canonical ownership

```text
FrameDeltaCompiler
  calculates old/new damage, effect spread, and DirtyTileMask
        |
        v
ExecutionResolver
  owns the only SparseTiles/FullRgb decision and coalesces regions
        |
        v
TileExecutionCoordinator
  executes the immutable FrameExecutionPlan
        |
        v
node_runner / tile_pruning
  applies only per-node intersection clips and skip results
```

`FrameDeltaCompiler` must not select an execution path. It produces geometry
and change facts only. `ExecutionResolver` is the sole owner of frame-level
policy: dirty-ratio fallback, spatial-effect safety, missing-history fallback,
empty-damage reuse, cost-model fallback, and tile-region coalescing.

The coordinator does not recalculate eligibility or policy. It consumes only
`FrameExecutionPlan::path`, `dirty_tiles`, and `dirty_regions`. The executor
helper `compute_dirty_clip()` performs a local bbox intersection for an already
selected tile and cannot enable/disable tile execution.

## Result

There is one tile-prune decision authority and no parallel policy between the
compiler, resolver, and coordinator. `TileExecutionPolicy` remains only as a
source compatibility alias for `ExecutionResolver`; it is not a second
implementation.
