# Track Matte 3D Source — Design

**Date:** 2026-05-17

## Problem

When a matte source layer is marked `is_3d`, it is built as a 2D item because `make_2d_item` hardcodes `projected = false`. The matte silhouette therefore ignores the camera 2.5D projection and appears at the wrong position/scale.

**Root cause:** `graph_builder_pipeline.cpp` line 74:
```cpp
LayerGraphItem matte_item = make_2d_item(*it->second);  // always projected=false
```

## Fix

Replace `make_2d_item` with `make_item_for_matte_source`. When the source layer is `is_3d` and the camera is enabled, compute the projection via `project_layer_2_5d()` (same path as regular 3D layers) and return an item with `projected=true` and the correct `projection_matrix`. Fall back to the existing 2D path otherwise.

## Files

- Modify: `src/render_graph/builder/graph_builder_pipeline.cpp` — replace `make_2d_item` lambda with `make_item_for_matte_source`; update call site at line 74.
- Modify: `tests/render_graph/test_track_matte.cpp` — add `Track matte 3D source: projection applies correctly`
- Modify: `docs/FUTURE_FEATURES.md` — mark track matte → completato

## Out of Scope

- Matte source parenting in 3D (world_transform already resolved by layer resolver)
- Luma matte + 3D (same fix applies automatically)
- Native-3D shapes as matte sources (handled by the `is_native_3d_layer` guard)
