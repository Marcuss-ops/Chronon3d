# TICKET — Demolition Debt: Legacy scene Fill compatibility model

Status: ACTIVE

Owner: Scene model and graphics-style maintainers.

## Reason

`graphics::GradientDefinition` is the canonical gradient authority, but the
scene model still exposes `FillType`, `GradientFill`, `Fill`, and factory /
sampling bridges in `include/chronon3d/scene/model/shape/fill.hpp`. These types
remain consumed by `PathShape`, render nodes, shape/text paint, software paths,
and `FillStyle::to_fill()`. Removing them before migrating those consumers
would be a source/API break and would leave shape fill semantics split across
representations.

## Exit condition

All production shape/path/render-node/text paint consumers store and consume
`graphics::FillStyle` (or the canonical `graphics::GradientDefinition` where a
solid/gradient variant is not required); there are no production references to
`FillType`, `GradientFill`, the scene `Fill` type, `fill_type_for_gradient`, or
the scene `sample_gradient*` forwarding functions. The remaining migration can
be measured by a zero-match scan of `include/chronon3d`, `src`, and `apps` for
those legacy declarations/usages, excluding this ticket and the retirement gate.

## Equivalence test/gate

- Scene shape/path and text gradient tests remain green.
- `chronon3d_render_plan_shape_tests` and `chronon3d_deterministic_tests` pass.
- Architecture rule `legacy_scene_fill_api_is_scoped_to_compatibility_boundary`
  rejects reintroducing a second gradient model and keeps references visible.
- Full `linux-fast-dev` build passes after the final call-site migration.

## Removal scope

1. `include/chronon3d/scene/model/shape/fill.hpp` legacy `FillType`,
   `GradientFill`, `Fill`, factories, and sampling forwarding helpers.
2. Consumers in `path.hpp`, `shape_stroke.hpp`, `render_node.hpp`,
   `graphics/shape_style/fill_style.hpp`, render-node factories and all
   production materializers/rasterizers.
3. Test and fixture call sites using the retired scene `Fill` surface.

## Migration inventory — census 2026-09-24 (`main@10843eb12`)

Pattern scanned: `\b(FillType|GradientFill|fill_type_for_gradient)\b` plus the
scene `Fill` type. **15 files, ~80 references.** Grouped by migration order:

**A. Storage / scene model (source of truth to switch first)**
1. `include/chronon3d/scene/model/render/render_node.hpp:44` — `Fill fill{...}` member.
2. `include/chronon3d/scene/model/shape/path.hpp:65,71` — `std::optional<GradientFill> gradient` + `Fill fill`.
3. `include/chronon3d/scene/model/shape/shape_stroke.hpp:42` — `std::optional<GradientFill> gradient`.
4. `include/chronon3d/graphics/shape_style/fill_style.hpp` — the bridge itself:
   `to_fill()` / `FillStyle::to_fill()` (keep as the *last* thing to delete, or
   move the conversion into the compatibility header).

**B. Production consumers (behaviour-affecting — migrate with pixel gates)**
5. `src/backends/software/blend2d/blend2d_paint.cpp` — `FillType::Solid/Linear/Radial/Conic` switch.
6. `src/backends/software/blend2d/blend2d_path_stroker.cpp:108-115` — stroke gradient → `FillType`.
7. `src/backends/software/rasterizers/shape_rasterizer_helpers.hpp:23,129,147`.
8. `src/backends/software/rasterizers/shape_rasterizer.cpp`.
9. `src/backends/software/rasterizers/path/path_rasterizer.cpp:171,232,356`.
10. `src/backends/software/processors/software_shape_processor.cpp:18`.
11. `src/backends/vulkan/vulkan_native_shape_dispatch.cpp:22,251`.
12. `src/compiler/scene_materializer.cpp`.
13. `src/render_graph/nodes/source_node_execute.cpp`.
14. `src/render_plan/render_plan_compiler.cpp:346` — `fill_type_for_gradient(...)`.
15. `tests/` and fixtures using the retired surface.

### Ordered, verifiable execution

1. Add `graphics::FillStyle` storage alongside the legacy members (no removal yet);
   keep `to_fill()` as the one conversion so both models agree byte-for-byte.
2. Migrate group **B** one file per commit, running after each:
   `chronon3d_render_plan_shape_tests`, `chronon3d_deterministic_tests`,
   `chronon3d_backends_software_tests`, `chronon3d_scene_tests`. Any pixel
   drift means the site still reads the legacy derivation.
3. Migrate group **A**, then delete `to_fill()` and the legacy declarations in
   `include/chronon3d/scene/model/shape/fill.hpp`.
4. Update the architecture contract in `tools/architecture_rules_shape.toml`
   (`legacy_scene_fill_api_is_scoped_to_compatibility_boundary` requires the
   legacy patterns to exist) and remove `fill.hpp` from
   `cmake/Chronon3DPublicHeaders.cmake` if the header is dropped.
5. Exit check: zero-match scan of `include/`, `src/`, `apps/` for
   `FillType`, `GradientFill`, `fill_type_for_gradient`, scene `Fill`.

Note: `include/chronon3d/scene/model/shape/fill.hpp` is a **public installed
header** (`cmake/Chronon3DPublicHeaders.cmake`), so removing it is an SDK surface
change and needs its own ABI window — factor that into the schedule.

## Status

ACTIVE. The render-plan Tier 1 closeout retains this compatibility API because
its production consumers have not yet migrated; it does not authorize new
consumers or a parallel gradient definition.
