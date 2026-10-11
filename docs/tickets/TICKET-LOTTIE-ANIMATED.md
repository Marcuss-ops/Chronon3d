# TICKET-LOTTIE-ANIMATED — Bounded Bodymovin lowering

## Stato

IN PROGRESS — L1 animated layer transforms, L2 bounded static group transforms,
L3 static gradients, dashed strokes, baked static operators, and bounded
two-keyframe L4 morphs lower through existing plans. The CLI MP4 frame-hash gate
is complete. L5 adds animated Trim Paths through the native trim animation
track; focused C ABI coverage and a CLI MP4 frame-hash gate now pass. L4 now
supports piecewise morphs with multiple matching-topology keyframes. L6 adds
animated solid fill/stroke color, opacity, and stroke width through native
imported-path property tracks. L8 now lowers bounded group position, uniform
scale, rotation, and opacity tracks through imported-path sampling.
L9 now lowers bounded static path masks with add/subtract/intersect mode,
inversion, static feather/expansion and animated opacity; L10 lowers two-keyframe
matching-topology mask path morphs through Chronon's existing mask Morph track.

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
- L4 accepts one geometry path with 2 to 256 integral-frame shape keyframes,
  matching vertex/tangent/closure topology, and per-segment linear or hold
  interpolation. It lowers to the existing animated Morph path operator;
  temporal Bezier handles, multi-path morphing, and incompatible topology fail
  closed.
- L5 accepts Lottie `tm` Trim Paths with static or animated start, end, and
  offset. Animated properties share integral keyframe times and easing, and
  lower into the existing native `trim_animation` track. Values convert from
  Lottie percent/degrees to Chronon normalized fractions. Simultaneous mode is
  supported when Trim Paths is the only path operator; individual-path mode and
  operator combinations fail closed.
- L6 accepts animated solid fill/stroke RGBA color and opacity plus stroke
  width, lowering to the existing sampled imported-path paint fields. Keyframes
  retain native timing/easing; malformed or unsupported values fail closed.
- L8 accepts animated group position, uniform scale, rotation and opacity. A
  single animated group may contain static nested groups; animated groups below
  transformed/opaque ancestors or another animated group fail closed because
  those matrix stacks are not flattened into lossy component tracks.
- L9 accepts up to 16 static Lottie path masks with add, subtract or intersect
  mode, inversion, static scalar/uniform-Vec2 feather, static expansion, and
  static or animated opacity.
- L10 accepts exactly two integral-frame mask path keyframes with matching
  vertex/tangent/closure topology and linear or hold interpolation. Multi-
  keyframe mask morphs and temporal Bezier easing fail closed.
- Animated anchors, split dimensions, expressions, unsupported mask paths, text, precompositions,
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
- [x] A CLI software fixture renders an MP4 with distinct frame hashes at the
  authored keyframes; input and command limits remain enforced.
  (`golden/lottie_cli_gate/{mark_anim.json,plan.json}` — 392 B L1 animated
  rect, keyframes t=0/t=30 — checked by
  `scripts/ci/check_lottie_cli_frame_gate.py`: frame 0
  `088168c2…` vs frame 30 `2a589ae1…`, distinct.)
- [x] Focused Lottie C ABI/render-plan tests pass (software scene evaluation).
- [x] Focused L4 two-keyframe morph test passes and verifies distinct evaluated
  endpoint geometry.
- [x] Focused L4 multi-keyframe morph test verifies intermediate values across
  two segments and the final endpoint.
- [x] Focused L8 group test verifies animated translation, scale, rotation,
  opacity and stroke-width sampling at start, intermediate and final frames
  (Lottie-focused C ABI: 9/9 cases, 166/166 assertions).
- [x] L9 focused C ABI test verifies static mask path conversion, subtract mode,
  uniform Vec2 feather, expansion, and animated opacity at start, intermediate
  and final frames.
- [x] L10 focused mask test verifies intermediate morph geometry and rejects
  incompatible mask topology (Lottie-focused C ABI: 9/9 cases, 173/173
  assertions).
- [x] Complete C ABI suite with SVG enabled passes after the Lottie importer
  work and SVG extensions (246/246 cases, 6296/6296 assertions;
  `build/chronon/linux-fast-dev/tests/chronon3d_c_abi_tests`).
- [x] CLI software fixture and frame-hash gate pass.
- [x] L5 animated Trim Paths fixture verifies lowering, unit conversion,
  interpolation preservation, and unsupported mode/timing diagnostics.
- [x] CLI software MP4 fixture renders animated Trim Paths with distinct
  frame hashes: frame 0 `a1ff5f59…`, frame 30 `c6bb7aba…`.
- [x] Focused L6 test checks animated fill color/opacity and stroke width at an
  intermediate frame and endpoint (C ABI: 1 case, 17 assertions).

## Demolition debt

- **Status:** IN PROGRESS — retain bounded lowering; L5 gates are complete and
  broader importer scope remains deferred behind a separate requirement.
- **Owner:** Chronon3D render-plan maintainers.
- **Reason:** Bodymovin documents must lower into Chronon's canonical plan.
- **Exit condition:** remove only if Bodymovin import is retired or replaced by
  an equivalent canonical importer with the acceptance gates above.
- **Equivalence gate:** matching lowered fingerprints and software frame hashes
  for the supported fixture corpus.
- **Removal scope:** `render_plan_compiler_lottie.cpp`, its resolver integration,
  and Lottie-specific tests and diagnostics.

## Local focused-test refresh — 2026-10-11

Re-ran `.tmp/chronon-builds/linux-fast-dev/tests/chronon3d_c_abi_tests
--test-case='*Lottie*'`: **9/9 cases and 173/173 assertions passed**. The binary
was newer than the maintained Lottie test sources; the worktree is dirty, so
this is local CPU evidence, not clean-SHA CI. The separate CLI frame-hash gates
remain the evidence for rendered MP4 output.
