# TICKET-LOTTIE-ANIMATED — Bounded Bodymovin lowering

## Stato

IN PROGRESS — L1 animated layer transforms, L2 bounded static group transforms,
L3 static gradients, dashed strokes, and baked static operators, plus bounded
two-keyframe L4 morphs lower through existing plans. The CLI MP4 frame-hash gate
and broader piecewise morph support remain open.

## Contratto

- Bodymovin stays a compile-time input. Lowered geometry and transform tracks
  use `LayerPlan`, `AnimationTrackPlan`, the existing sampler and the existing
  scene compiler; no second animation engine is introduced.
- L1 supports 2D shape layers with static or animated position, scale, Z
  rotation and opacity, plus a static anchor. Static anchors are baked into
  path coordinates. Keyframe times are integral Bodymovin frames; hold,
  linear and temporal Bezier easing lower to native keyframe timing. Bezier
  handles are clamped to the existing validator bounds.
- L2 recursively flattens groups up to depth 16 and 256 authored shape items.
  Static group position, anchor, scale, rotation and opacity compose into path
  geometry/paint at lowering time. Radial gradients and strokes fail closed
  under non-uniform group transforms because their existing scalar width/radius
  contracts cannot represent the result faithfully.
- L3 imports bounded static linear/radial gradients and dash/gap/offset stroke
  entries into the existing gradient and stroke payloads. Static Bodymovin
  repeater, round-corners, zig-zag, offset-path, and pucker/bloat operators are
  baked during lowering with a 256-copy and 4,096-output-path bound.
- L4 accepts one geometry path with exactly two integral-frame shape keyframes,
  matching vertex/tangent topology, and linear or hold interpolation. It lowers
  to the existing animated Morph path operator; extra keyframes, temporal
  Bezier handles, multi-path morphing, and incompatible topology fail closed.
- Animated anchors, split dimensions, expressions, masks, text, precompositions,
  3D layers, unsupported shape-level transforms and unsupported interpolation
  fail closed with a Lottie lowering diagnostic.
- Input remains limited to 10 MiB; emitted commands remain limited to 65,536
  by the existing path lowering. Fingerprints include the resulting native
  transform fields and tracks through the existing plan fingerprint.
- Capability status: software scene evaluation `PASS` for L1-L3 supported
  geometry/paint; GPU
  capability is outside this headless ticket and is not required for L1
  acceptance.

## Acceptance gates

- [x] L1 emits only existing plan track/property types and preserves static
  Lottie rendering through the same path compiler.
- [x] CPU tests cover linear, hold and Bezier interpolation, static transform
  values, static nested group transforms/opacity, anchor baking, gradient fills,
  dashed strokes, repeater expansion/opacity, changing frame output, unsupported
  anchor animation, and typed failure for unsupported Lottie features.
- [x] Render-plan fingerprint changes when a Lottie transform keyframe value
  changes; the native track fields use the existing plan fingerprint path.
- [ ] A CLI software fixture renders an MP4 with distinct frame hashes at the
  authored keyframes; input and command limits remain enforced.
- [x] Focused Lottie C ABI/render-plan tests pass (software scene evaluation).
- [x] Focused L4 two-keyframe morph test passes and verifies distinct evaluated
  endpoint geometry.
- [ ] CLI software fixture and frame-hash gate pass.

## Demolition debt

- **Status:** ACTIVE — importer is the bounded Bodymovin compatibility path.
- **Owner:** Chronon3D render-plan maintainers.
- **Reason:** Bodymovin documents must lower into Chronon's canonical plan.
- **Exit condition:** remove only if Bodymovin import is retired or replaced by
  an equivalent canonical importer with the acceptance gates above.
- **Equivalence gate:** matching lowered fingerprints and software frame hashes
  for the supported fixture corpus.
- **Removal scope:** `render_plan_compiler_lottie.cpp`, its resolver integration,
  and Lottie-specific tests and diagnostics.
