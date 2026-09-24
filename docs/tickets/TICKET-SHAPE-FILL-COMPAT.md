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

## Status

ACTIVE. The render-plan Tier 1 closeout retains this compatibility API because
its production consumers have not yet migrated; it does not authorize new
consumers or a parallel gradient definition.
