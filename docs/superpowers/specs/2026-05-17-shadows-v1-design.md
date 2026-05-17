# Shadows V1 Design

**Date:** 2026-05-17  
**Branch:** `codex/shadows-v1`  
**Scope:** Projected 2.5D shadows — directional light only, CPU-only, deterministic output.

---

## Goals

Add visible, testable projected shadows to Chronon3d's 2.5D pipeline. A layer marked `casts_shadows` projects its alpha silhouette onto a layer marked `accepts_shadows`, offset by the scene's directional light direction and the world-Z separation between the two layers.

Non-goals for V1: point/spot lights, shadow maps, ray tracing, PBR, perspective-correct shadow distortion, auto-scale from projection matrix, shared caster transform outputs.

---

## Components

### 1. Material & Builder (wire-up only)

`Material2_5D` already has the required fields — they are dormant:

```cpp
bool casts_shadows{false};
bool accepts_shadows{true};
```

Add two methods to `LayerBuilder` (`layer_builder.hpp` / `layer_builder.cpp`):

```cpp
LayerBuilder& casts_shadows(bool value = true);   // sets material.casts_shadows
LayerBuilder& accepts_shadows(bool value = true); // sets material.accepts_shadows
```

No other changes to `Material2_5D`.

---

### 2. ShadowSettings

New header: `include/chronon3d/rendering/shadow_settings.hpp`

```cpp
struct ShadowSettings {
    f32 opacity{0.35f};       // shadow alpha multiplier
    f32 blur_radius{8.0f};    // Gaussian blur in pixels
    f32 px_per_unit{1.0f};    // world units → screen pixels scale (explicit V1)
    f32 max_offset{300.0f};   // clamp on screen-space offset
};
```

Added to `LightContext` as a member:

```cpp
ShadowSettings shadows{};
```

`px_per_unit` is explicit in V1. Deriving it from the projection matrix is deferred to V2.

---

### 3. ShadowCasterInfo

Defined in `graph_builder_shadow_pass.hpp`:

```cpp
struct ShadowCasterInfo {
    const Layer*  layer{nullptr};     // for building source node on-demand
    Mat4          world_matrix{1.0f}; // for transform node
    f32           world_z{0.0f};      // for shadow offset math
    bool          projected{false};
};
```

`layer->name` is used for the cache key. `layer->material.casts_shadows` is checked inside the pass. No pre-built `transformed_output` — the pass builds caster source+transform on-demand. Passed as `std::span<const ShadowCasterInfo>` to keep the pass decoupled from raw `LayerGraphItem` internals.

---

### 4. ShadowNode

New file: `include/chronon3d/render_graph/nodes/shadow_node.hpp`

**Input:** caster framebuffer (post-transform, RGBA).  
**Params:** `caster_world_z`, `receiver_world_z`, `light_direction`, `ShadowSettings`.  
**Output:** black transparent framebuffer — the blurred, offset silhouette.

**Execute logic:**
1. Allocate output framebuffer (same dimensions, cleared to transparent).
2. For each pixel in caster fb: write `(0, 0, 0, src_alpha * opacity)` at translated position.
3. Translation (screen space):
   ```cpp
   const float dz = caster_world_z - receiver_world_z;
   const float safe_y = std::abs(light.y) > 1e-4f
       ? light.y
       : std::copysign(1e-4f, light.y != 0.f ? light.y : 1.f);
   float ox = -(light.x / safe_y) * dz * settings.px_per_unit;
   float oy = -(light.z / safe_y) * dz * settings.px_per_unit;
   ox = std::clamp(ox, -settings.max_offset, settings.max_offset);
   oy = std::clamp(oy, -settings.max_offset, settings.max_offset);
   ```
4. Apply Gaussian blur with `blur_radius`.
5. Cache key includes: caster name, dz, light direction, opacity, blur_radius, px_per_unit, frame size.

`RenderGraphNodeKind`: reuse `Effect` for V1 (no new enum value needed).

---

### 5. Shadow Pass

New files: `src/render_graph/builder/passes/graph_builder_shadow_pass.hpp` / `.cpp`

```cpp
void append_shadow_passes_if_needed(
    RenderGraph& graph,
    GraphNodeId& receiver_output,
    const LayerGraphItem& receiver_item,
    std::span<const ShadowCasterInfo> casters,
    const RenderGraphContext& ctx
);
```

**Receiver gating:**
```cpp
if (!receiver_item.layer->material.accepts_shadows) return;
if (!ctx.light_context.enabled || !ctx.light_context.directional_enabled) return;
if (!receiver_item.projected) return;
```

**Per-caster gating:**
```cpp
if (!caster.layer->material.casts_shadows) continue;
if (!caster.projected) continue;
if (caster.layer == receiver_item.layer) continue;
if (std::abs(caster.world_z - receiver_item.world_z) < 1e-3f) continue;
```

**Per caster — build on-demand sub-pipeline:**
```
caster Source → caster Transform → ShadowNode → Composite(BlendMode::Normal) onto receiver_output
```

This intentionally may rebuild caster source+transform. V2 can introduce shared transform output caching. Keeps V1 from touching draw order, track matte, adjustment layers, 3D bin, precomp/video flows.

---

### 6. Integration into Layer Pipeline

In `graph_builder_layer_pipeline.cpp`, add one call after the lighting pass:

```cpp
append_lighting_pass_if_needed(graph, layer_output, item, ctx);
append_shadow_passes_if_needed(graph, layer_output, item, casters, ctx);   // NEW
append_effect_pass_if_needed(graph, layer_output, item, ctx);
```

`casters` is a `std::span<const ShadowCasterInfo>` built by the caller (`graph_builder_pipeline.cpp`) before the layer loop. It collects all layers where `material.casts_shadows && projected`.

**Resulting receiver pipeline:**
```
Source → Transform → Lighting → [ShadowNode×n → Composite] → Effects → TrackMatte → Composite
```

---

## New Files

```
include/chronon3d/rendering/shadow_settings.hpp
include/chronon3d/render_graph/nodes/shadow_node.hpp
src/render_graph/builder/passes/graph_builder_shadow_pass.hpp
src/render_graph/builder/passes/graph_builder_shadow_pass.cpp
tests/renderer/lighting/test_shadows.cpp
```

## Modified Files

```
include/chronon3d/rendering/light_context.hpp          (add ShadowSettings shadows{})
include/chronon3d/scene/builders/layer_builder.hpp     (add casts_shadows / accepts_shadows)
src/scene/layer_builder.cpp                            (implement the two methods)
src/render_graph/builder/passes/graph_builder_shadow_pass.hpp  (declare ShadowCasterInfo)
src/render_graph/builder/graph_builder_layer_pipeline.cpp      (add shadow pass call)
src/render_graph/builder/graph_builder_pipeline.cpp    (build casters span before layer loop)
tests/CMakeLists.txt                                   (add test_shadows.cpp)
docs/FUTURE_FEATURES.md                                (mark shadows V1 done)
```

---

## Tests

File: `tests/renderer/lighting/test_shadows.cpp`

| # | Name | What it verifies |
|---|------|-----------------|
| 1 | `shadow_visible_on_receiver` | floor receives shadow from card with casts/accepts both true; pixel under card darker |
| 2 | `no_shadow_when_casts_false` | caster `casts_shadows=false`; pixel unchanged |
| 3 | `no_shadow_when_accepts_false` | receiver `accepts_shadows=false`; pixel unchanged |
| 4 | `light_direction_flips_shadow_horizontal` | two renders with mirrored light x; shadow offsets are mirrored; hashes differ |
| 5 | `z_distance_changes_shadow_offset` | larger `dz` → larger offset |
| 6 | `parented_caster_uses_world_z` | caster parented, world_z resolved; shadow follows correctly |
| 7 | `near_zero_light_y_does_not_explode` | `light_dir = {1, 0.0001, 0}`; no NaN/crash; offset clamped |

---

## Debug Output (optional, not committed)

```
output/debug/lighting/shadows/
  01_no_shadow.png
  02_basic_projected_shadow.png
  03_shadow_direction_left.png
  04_shadow_direction_right.png
  05_parented_caster_shadow.png
  06_soft_shadow_blur.png
```

These are written only when the test binary runs under a debug flag. Never committed.

---

## Out of Scope (V1)

- Point/spot lights
- Multiple directional lights
- Shadow maps / ray tracing / PBR
- Auto-derive `px_per_unit` from projection matrix
- Shared caster transform outputs (V2 optimization)
- Colored shadows
- Shadow on native 3D geometry
- Shadow on precomp / video layers
