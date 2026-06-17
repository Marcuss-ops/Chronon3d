# Renderer State Refactoring — June 2026

> Migrated from `README.md` on 2026-06-17.

## Renderer State Refactoring

The `SoftwareRenderer` internal state has been refactored into dedicated aggregates:

| Type | Header | Purpose |
|---|---|---|
| `RendererFrameHistory` | `renderer_types.hpp` | Per-frame camera + fingerprint history |
| `RendererDirtyTelemetry` | `renderer_types.hpp` | Dirty-rect / tile-execution telemetry |
| `RendererLayerHistory` | `renderer_types.hpp` | Per-layer bbox state for dirty-rect diffing |
| `LayerBBoxState` | `renderer_types.hpp` | Per-layer bounding box + diff state |
| `RendererBufferRing` | `buffer_ring.hpp` | managed ping-pong framebuffer ring |
| `TransformScratchBuffer` | `scratch_buffer.hpp` | managed transform scratch buffer |
| `CompiledGraphCache` | `graph_cache.hpp` | managed cached compiled render graph |

**SoftwareRenderer accessors** — `SoftwareRenderer` now exposes `.frame_history()`, `.dirty_telemetry()`, `.layer_history()`, `.buffer_ring()`, `.scratch_buffer()`, and `.graph_cache()` instead of the old direct public members.

**Deleted headers:**
- `include/chronon3d/backends/software/software_renderer_internal.hpp` — removed; migrate includes to the four new headers above.

## Breaking Changes

- **`TextAnchor` is now an `enum class`** (was a struct). Remove `.align` / `.padding` accessors — use `TextStyle::align` and `TextStyle::vertical_align` directly.
- **`SceneBuilder` / `LayerBuilder` includes are no longer transitive.** Add `#include <chronon3d/scene/builders/scene_builder.hpp>` explicitly in your code.
- **`project_layer_2_5d()`** Mat4 overload gains `bool diagnostics_enabled = false` default parameter.
